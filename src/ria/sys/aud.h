/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _AUD_H_
#define _AUD_H_

#include <stddef.h>
#include <stdint.h>

/* Kernel events
 */

void aud_init(void);
void aud_reclock(void);
void aud_stop(void);
void aud_task(void);

void aud_print_status(void);

// external interface
void aud_pwm_set_channel(size_t channel, uint16_t freq, uint8_t duty);
void aud_pwm_set_channel_duty(size_t channel, uint8_t duty);

#endif /* _AUD_H_ */
