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

// 16MB of XIP QPI PSRAM interface
uint8_t mem_read_psram(uint32_t addr);
void mem_write_psram(uint32_t addr, uint8_t data);
void mem_cpy_psram(uint32_t dest_addr, const void *src, size_t n);

// Read/Write memory or overlaying memory mapped device.
// Similar function as the CPU BUS mapper, but for firmware code.
uint8_t mem_read_byte(uint32_t addr);
void mem_write_byte(uint32_t addr, uint8_t data);

// Move data from the RAM to mbuf.
void mem_read_buf(uint32_t addr);

// Move data from mbuf to the RAM.
void mem_write_buf(uint32_t addr);

#endif /* _MEM_H_ */
