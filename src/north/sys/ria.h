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

/* Main events
 */

void ria_init(void);
void ria_task(void);
void ria_run();
void ria_stop();

// Trigger IRQ when enabled
void ria_trigger_irq(void);

void ria_print_status(void);

// read/write memory or RIA registers
uint8_t ria_read_mem(uint32_t addr24);
void ria_write_mem(uint32_t addr24, uint8_t data);

// Move data from the RAM to mbuf.
void ria_read_buf(uint32_t addr24);
// Move data from mbuf to the RAM.
void ria_write_buf(uint32_t addr24);

#endif /* _RIA_SYS_RIA_H_ */
