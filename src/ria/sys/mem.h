/*
 * Copyright (c) 2023 Rumbledethumps
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _MEM_H_
#define _MEM_H_

#include "cgia/cgia.h"
#include "hardware/gpio.h"
#include "main.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// PSRAM chips
#define PSRAM_BANKS_NO 2
extern size_t psram_size[PSRAM_BANKS_NO];
extern uint8_t psram_readid_response[PSRAM_BANKS_NO][8];
#define XIP_PSRAM_CACHED  0x11000000
#define XIP_PSRAM_NOCACHE 0x15000000

// The xstack is:
// 512 bytes, enough to hold a CC65 stack frame, two strings for a
// file rename, or a disk sector
// 1 byte at end+1 always zero for cstring and safety.
// Using xstack for cstrings doesn't require sending the zero termination.
#define XSTACK_SIZE 0x200
extern uint8_t xstack[];
extern volatile size_t xstack_ptr;

// RP816 RIA "internal" registers
extern volatile uint8_t __regs[0x40];
#define REGS(addr)   (__regs[(addr) & 0x3F])
#define REGSW(addr)  (((volatile uint16_t *)&REGS(addr))[0])
#define REGSDW(addr) (((volatile uint32_t *)&REGS(addr))[0])

// Misc memory buffer for moving things around.
// FS <-> RAM, USB <-> RAM, UART <-> RAM, etc.
#define MBUF_SIZE 1024
extern uint8_t mbuf[];
extern size_t mbuf_len;

// Compute CRC32 of mbuf to match zlib.
uint32_t mbuf_crc32(void);

/* Kernel events
 */

void mem_init(void);
void mem_task(void);
void mem_post_reclock(void);
void mem_print_status(void);

// 16MB of XIP QPI PSRAM interface
// accessed through fast L2 cache implemented in internal SRAM

// in 2 banks of 8MB each
__force_inline static void mem_select_bank(bool bank)
{
    gpio_put(QMI_PSRAM_BS_PIN, bank);
}

// ---------------------------------------------------------------
// L2 memory cache
// ---------------------------------------------------------------
// Cache Size: 64 kB
// Cache Line Size: 32 Bytes [XIP fast fetch]
//
// We split the incoming 65C816 address into three parts to look up data:
// [AAAA AAAA][BBBB BBBB BBB][CCCCC]
// Offset (5 bits): Which of the 32 bytes in the line do we want?
// Index (11 bits): Which of the 2048 cache lines do we check? ($2^{11} = 2048$)
// Tag (8 bits): The remaining upper bits. We store this to verify if the cache
//               line actually holds the memory we asked for.

#define MEM_L2_LINE_SIZE   32
#define MEM_L2_LINE_COUNT  2048
#define MEM_L2_LINE_MASK   (MEM_L2_LINE_COUNT - 1) // 0x7FF
#define MEM_L2_OFFSET_MASK (MEM_L2_LINE_SIZE - 1)  // 0x1F
#define MEM_L2_TAG_MASK    0xFF
#define MEM_L2_TAG_VALID   0x100

// The Data Store: 64kB
extern uint8_t l2_data[MEM_L2_LINE_COUNT][MEM_L2_LINE_SIZE];
// The Tag Store: 2048 entries of 8-bit tag plus valid bit.
// A 16-bit int is faster to align/access than a packed byte struct.
extern uint16_t l2_tags[MEM_L2_LINE_COUNT];

__force_inline static void
mem_l2_fill_line(uint32_t *dest, const uint32_t *src_nocache)
{
    __asm volatile(
        "ldmia %1!, {r0-r3}\n\t" // Burst Load 4 words (16B) from PSRAM
        "stmia %0!, {r0-r3}\n\t" // Burst Store 4 words (16B) to SRAM
        "ldmia %1!, {r0-r3}\n\t" // Repeat to complete 32B
        "stmia %0!, {r0-r3}\n\t"
        : "+r"(dest), "+r"(src_nocache)    // Outputs (pointers define the address)
        :                                  // No other inputs
        : "r0", "r1", "r2", "r3", "memory" // Clobbers
    );
}

// The bus ISR and the kernel loop both access PSRAM on core 0.
// These two are for the bus ISR only. They live here so they inline
// into the ISR; nothing must slow the bus cycle.
__force_inline static uint8_t mem_read_ram_isr(uint32_t addr24)
{
    const uint16_t index = (addr24 >> 5) & MEM_L2_LINE_MASK;
    const uint16_t tag = ((addr24 >> 16) & MEM_L2_TAG_MASK) | MEM_L2_TAG_VALID;
    if (l2_tags[index] != tag)
    {
        // Cache miss - fetch the cache line from PSRAM
        mem_select_bank(addr24 & 0x800000);
        mem_l2_fill_line((uint32_t *)l2_data[index],
                         (const uint32_t *)(XIP_PSRAM_NOCACHE | (addr24 & 0x7FFFE0)));
        l2_tags[index] = tag;
    }
    return l2_data[index][addr24 & MEM_L2_OFFSET_MASK];
}

__force_inline static void mem_write_ram_isr(uint32_t addr24, uint8_t data)
{
    // L2 write-through cache
    mem_select_bank(addr24 & 0x800000);
    *(volatile uint8_t *)(XIP_PSRAM_NOCACHE | (addr24 & 0x7FFFFF)) = data;

    // Update L2 cache if present
    const uint16_t index = (addr24 >> 5) & MEM_L2_LINE_MASK;
    const uint16_t tag = ((addr24 >> 16) & MEM_L2_TAG_MASK) | MEM_L2_TAG_VALID;
    if (l2_tags[index] == tag)
    {
        l2_data[index][addr24 & MEM_L2_OFFSET_MASK] = data;
    }

    // Sync write to CGIA L1 cache
    cgia_ram_write((uint8_t)(addr24 >> 16), (uint16_t)addr24, data);
}

// These are for the kernel loop: they keep the bus ISR out while they
// select a bank and use the L2 cache.
uint8_t mem_read_ram(uint32_t addr24);
void mem_write_ram(uint32_t addr24, uint8_t data);

// Copy a buffer into PSRAM, one cache row per interrupt window.
void mem_cpy(uint32_t dest_addr24, const void *src, size_t len);

// Fetch a PSRAM cache row (32 bytes) and return a pointer to it.
// Call with interrupts disabled.
uint8_t *mem_fetch_row(uint8_t bank, uint16_t addr);

// Read/Write memory or overlaying memory mapped device.
// Similar function as the CPU BUS mapper, but for firmware code.
uint8_t mem_read_byte(uint32_t addr);
void mem_write_byte(uint32_t addr, uint8_t data);

// Move data from the RAM to mbuf.
void mem_read_buf(uint32_t addr);

// Move data from mbuf to the RAM.
void mem_write_buf(uint32_t addr);

#endif /* _MEM_H_ */
