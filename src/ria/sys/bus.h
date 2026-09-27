/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _BUS_H_
#define _BUS_H_

#include <stdbool.h>
#include <stdint.h>

/* Kernel events
 */

void bus_init(void);
void bus_task(void);
void bus_run(void);
void bus_stop(void);

void bus_print_status(void);

// 65816 PHI2 clock in kHz, set by the bus PIO timing
uint16_t bus_get_phi2_khz(void);

// CIA timers interrupt request, gated by IRQ_ENABLE ($FFEC)
void bus_set_cia_irq(bool asserted);

#endif /* _BUS_H_ */
