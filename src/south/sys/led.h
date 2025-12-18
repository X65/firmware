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

// RGB LED control
void led_set_pixel(size_t index, uint8_t r, uint8_t g, uint8_t b);
void led_set_pixel_rgb332(size_t index, uint8_t rgb332);

#endif /* _VPU_SYS_LED_H_ */
