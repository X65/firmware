/*
 * Copyright (c) 2025 Rumbledethumps
 * Copyright (c) 2025 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _VPU_SYS_LED_H_
#define _VPU_SYS_LED_H_

/* System LED control
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Main events
 */

void led_init(void);
void led_task(void);

// Make it blink
void led_blink(bool on);
void led_blink_color(uint32_t grb);

// RGB LED control
void led_put(uint32_t grb);

#endif /* _VPU_SYS_LED_H_ */
