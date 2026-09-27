/*
 * Copyright (c) 2026 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _CIA_H_
#define _CIA_H_

/* 6526 CIA compatible timers at $FF98-$FF9F, counting at 1 MHz.
 * Only the timers and the interrupt control register are exposed:
 *   0 TA LO, 1 TA HI, 2 TB LO, 3 TB HI, 4 (SDR, unused), 5 ICR, 6 CRA, 7 CRB
 */

#include <stdbool.h>
#include <stdint.h>

/* Kernel events
 */

void cia_init(void);
void cia_stop(void);

// Register access for the bus ISR. offset is 0..7.
uint8_t cia_reg_read(uint8_t offset);
void cia_reg_write(uint8_t offset, uint8_t data);

// Interrupt request line (ICR bit 7)
bool cia_irq(void);

#endif /* _CIA_H_ */
