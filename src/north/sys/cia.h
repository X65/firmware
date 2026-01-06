/*
 * Copyright (c) 2025 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RIA_SYS_CIA_H_
#define _RIA_SYS_CIA_H_

/* CIA-like timers control
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Main events
 */

void cia_init(void);
void cia_task(void);

enum cia_timer_id
{
    CIA_A = 0,
    CIA_B = 1,
};

uint16_t cia_get_count(enum cia_timer_id t_id);
uint8_t cia_get_icr(void);
uint8_t cia_get_control(enum cia_timer_id t_id);

void cia_set_count_lo(enum cia_timer_id t_id, uint8_t value);
void cia_set_count_hi(enum cia_timer_id t_id, uint8_t value);
void cia_set_icr(uint8_t value);
void cia_set_control(enum cia_timer_id t_id, uint8_t value);

#endif /* _RIA_SYS_CIA_H_ */
