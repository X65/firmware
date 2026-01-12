/*
 * Copyright (c) 2023 Rumbledethumps
 * Copyright (c) 2024-2026 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RIA_SYS_MEM_H_
#define _RIA_SYS_MEM_H_

/* Various large chunks of memory used globally.
 */

#include "hw.h"
#include <hardware/gpio.h>
#include <hardware/xip_cache.h>
#include <pico.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// PSRAM
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

// RIA registers are located in uninitialized ram so they survive
// a soft reboot. A hard reboot with the physical button overwrites
// this memory which might be a security feature we can override.
extern volatile uint8_t regs[];
#define REGS(addr)   regs[(addr) & 0x3F]
#define REGSW(addr)  ((uint16_t *)&REGS(addr))[0]
#define REGSDW(addr) (((volatile uint32_t *)&REGS(addr))[0])

// Misc memory buffer for moving things around.
// FS <-> RAM, USB <-> RAM, UART <-> RAM, etc.
// Also used as a littlefs buffer for read/write.
#define MBUF_SIZE 1024
extern uint8_t mbuf[];
extern size_t mbuf_len;

// This is used by the monitor when in reset,
// and by modem emulation when CPU is running.
#define RESPONSE_BUF_SIZE 128
extern char response_buf[RESPONSE_BUF_SIZE];

// Compute CRC32 of mbuf to match zlib.
uint32_t mbuf_crc32(void);

/* Main events
 */

void ram_init(void);
int ram_status_response(char *buf, size_t buf_size, int state);

// 16MB of XIP QPI PSRAM interface

// accessed through fast L2 cache implemented in internal SRAM
#define MEM_USE_L2_CACHE (1)

// in 2 banks of 8MB each
__force_inline static void __attribute__((optimize("O3")))
mem_select_bank(bool bank)
{
#if !MEM_USE_L2_CACHE
    if (gpio_get(QMI_PSRAM_BS_PIN) != bank)
    {
        xip_cache_clean_all();
        xip_cache_invalidate_all();
        gpio_put(QMI_PSRAM_BS_PIN, bank);
    }
#else
    gpio_put(QMI_PSRAM_BS_PIN, bank);
#endif
}

#if MEM_USE_L2_CACHE
uint8_t mem_read_ram(uint32_t addr24);
void mem_write_ram(uint32_t addr24, uint8_t data);
#else
__force_inline static uint8_t __attribute__((optimize("O3")))
mem_read_ram(uint32_t addr24)
{
    // No L2 cache - direct read from PSRAM
    mem_select_bank(addr24 & 0x800000);
    return *(volatile uint8_t *)(XIP_PSRAM_CACHED | (addr24 & 0x7FFFFF));
}
__force_inline static void __attribute__((optimize("O3")))
mem_write_ram(uint32_t addr24, uint8_t data)
{
    // No L2 cache - direct write to PSRAM
    mem_select_bank(addr24 & 0x800000);
    *(volatile uint8_t *)(XIP_PSRAM_CACHED | (addr24 & 0x7FFFFF)) = data;
    // Sync write to CGIA L1 cache
    pix_mem_write(addr24, data);
}
#endif

// Fetch a PSRAM cache row (32 bytes) and return a pointer to it
uint8_t *mem_fetch_row(uint8_t bank, uint16_t addr);

#endif /* _RIA_SYS_MEM_H_ */
