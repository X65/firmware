/*
 * Copyright (c) 2025 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _SB_SYS_BUZ_H_
#define _SB_SYS_BUZ_H_

/* System BUZZER control
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Main events
 */

void buz_init(void);
void buz_task(void);

void buz_set_freq_duty(double freq, uint8_t duty);

void buz_set_freq16(uint16_t freq);
void buz_set_duty(uint8_t duty);

#endif /* _SB_SYS_BUZ_H_ */
