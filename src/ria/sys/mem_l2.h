/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _MEM_L2_H_
#define _MEM_L2_H_

// L2 PSRAM cache internals, for the bus ISR (bus.c) and mem.c only.
// Everything else goes through mem.h.

#include "cgia/cgia.h"
#include "mem.h"
#include <stdint.h>

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
extern uint8_t mem_l2_data[MEM_L2_LINE_COUNT][MEM_L2_LINE_SIZE];
// The Tag Store: 2048 entries of 8-bit tag plus valid bit.
// A 16-bit int is faster to align/access than a packed byte struct.
extern uint16_t mem_l2_tags[MEM_L2_LINE_COUNT];

__force_inline static uint16_t mem_l2_index(uint32_t addr24)
{
    return (addr24 >> 5) & MEM_L2_LINE_MASK;
}

// tag as stored in mem_l2_tags for a line holding addr24
__force_inline static uint16_t mem_l2_tag(uint32_t addr24)
{
    return ((addr24 >> 16) & MEM_L2_TAG_MASK) | MEM_L2_TAG_VALID;
}

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
// These two are for the bus ISR only. They are inline so nothing
// slows the bus cycle.
__force_inline static uint8_t mem_read_ram_isr(uint32_t addr24)
{
    const uint16_t index = mem_l2_index(addr24);
    const uint16_t tag = mem_l2_tag(addr24);
    if (mem_l2_tags[index] != tag)
    {
        // Cache miss - fetch the cache line from PSRAM
        mem_select_bank(addr24 & 0x800000);
        mem_l2_fill_line((uint32_t *)mem_l2_data[index],
                         (const uint32_t *)(XIP_PSRAM_NOCACHE | (addr24 & 0x7FFFE0)));
        mem_l2_tags[index] = tag;
    }
    return mem_l2_data[index][addr24 & MEM_L2_OFFSET_MASK];
}

__force_inline static void mem_write_ram_isr(uint32_t addr24, uint8_t data)
{
    // L2 write-through cache
    mem_select_bank(addr24 & 0x800000);
    *(volatile uint8_t *)(XIP_PSRAM_NOCACHE | (addr24 & 0x7FFFFF)) = data;

    // Update L2 cache if present
    const uint16_t index = mem_l2_index(addr24);
    if (mem_l2_tags[index] == mem_l2_tag(addr24))
    {
        mem_l2_data[index][addr24 & MEM_L2_OFFSET_MASK] = data;
    }

    // Sync write to CGIA L1 cache
    cgia_ram_write((uint8_t)(addr24 >> 16), (uint16_t)addr24, data);
}

#endif /* _MEM_L2_H_ */
