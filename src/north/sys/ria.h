/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RIA_SYS_RIA_H_
#define _RIA_SYS_RIA_H_

/* RP816 Interface Adapter for WDC W65C816.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// HID device selectors
#define RIA_HID_DEV_KEYBOARD 0x00
#define RIA_HID_DEV_MOUSE    0x01
#define RIA_HID_DEV_GAMEPAD  0x02

/* Main events
 */

void ria_init(void);
void ria_task(void);
void ria_run();
void ria_stop();

// Update IRQ state
#define RIA_IRQ_SOURCE_CIA 0x01
void ria_set_irq(uint8_t source);
void ria_clear_irq(uint8_t source);

int ria_status_response(char *buf, size_t buf_size, int state);

// read/write memory or RIA registers
uint8_t ria_read_mem(uint32_t addr24);
void ria_write_mem(uint32_t addr24, uint8_t data);

// Move data from the RAM to mbuf.
void ria_read_buf(uint32_t addr24);
// Move data from mbuf to the RAM.
void ria_write_buf(uint32_t addr24);

#endif /* _RIA_SYS_RIA_H_ */
