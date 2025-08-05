/*
 * Copyright (c) 2023 Rumbledethumps
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _MEM_H_
#define _MEM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 16MB of XIP QPI PSRAM interface
#define XIP_PSRAM_CACHED  0x11000000
#define XIP_PSRAM_NOCACHE 0x15000000
extern uint8_t psram[0x1000000]; // 16 MB of PSRAM address space
asm(".equ psram, 0x11000000");   // Addressable at 0x11000000 - 0x11ffffff

// PSRAM chips
#define PSRAM_BANKS_NO 2
extern size_t psram_size[PSRAM_BANKS_NO];
extern uint8_t psram_readid_response[8][PSRAM_BANKS_NO];

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
#define REGSW(addr)  (((uint16_t *)&REGS(addr))[0])
#define REGSDW(addr) (((uint32_t *)&REGS(addr))[0])

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
void mem_post_reclock(void);
void mem_print_status(void);

// Select PSRAM bank
void mem_use_bank(uint8_t bank);

// Move data from the RAM to mbuf.
void mem_read_buf(uint32_t addr);

// Move data from mbuf to the RAM.
void mem_write_buf(uint32_t addr);

#endif /* _MEM_H_ */
