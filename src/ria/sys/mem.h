/*
 * Copyright (c) 2023 Rumbledethumps
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RIA_SYS_MEM_H_
#define _RIA_SYS_MEM_H_

/* Various large chunks of memory used globally.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// PSRAM
#define XIP_PSRAM_CACHED  0x11000000
#define XIP_PSRAM_NOCACHE 0x15000000

// 64KB Extended RAM
#ifdef NDEBUG
extern uint8_t xram[];
#else
extern uint8_t *const xram;
#endif

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

// Compute CRC32 of mbuf to match zlib.
uint32_t mbuf_crc32(void);

/* Main events
 */

void mem_init(void);
void mem_print_status(void);

// 16MB of XIP QPI PSRAM interface
// accessed through fast L1 cache implemented in internal SRAM
uint8_t mem_read_ram(uint32_t addr24);
void mem_write_ram(uint32_t addr24, uint8_t data);

#endif /* _RIA_SYS_MEM_H_ */
