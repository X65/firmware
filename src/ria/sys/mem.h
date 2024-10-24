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
extern uint8_t psram[0x1000000]; // 16 MB of PSRAM address space
asm(".equ psram, 0x11000000");   // Addressable at 0x11000000 - 0x11ffffff

// The xstack is:
// 512 bytes, enough to hold a CC65 stack frame, two strings for a
// file rename, or a disk sector
// 1 byte at end+1 always zero for cstring and safety.
// Using xstack for cstrings doesn't require sending the zero termination.
#define XSTACK_SIZE 0x200
extern uint8_t xstack[];
extern volatile size_t xstack_ptr;

// RIA registers are located at the bottom of cpu1 stack.
// cpu1 runs the action loop and uses very little stack.
extern uint8_t regs[0x20];
#define REGS(addr)  regs[(addr) & 0x1F]
#define REGSW(addr) ((uint16_t *)&REGS(addr))[0]
asm(".equ regs, 0x20040000");

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
void mem_reclock(void);
void mem_print_status(void);

// Move data from the RAM to mbuf.
void mem_read_buf(uint32_t addr);

// Move data from mbuf to the RAM.
void mem_write_buf(uint32_t addr);

#endif /* _MEM_H_ */
