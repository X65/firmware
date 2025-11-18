/*
 * Copyright (c) 2025 Rumbledethumps
 * Copyright (c) 2025 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _VGA_TERM_TERM_H_
#define _VGA_TERM_TERM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Main events
 */

void term_init(void);
void term_task(void);

void term_render(int16_t y, uint32_t *rgbbuf);

#endif /* _VGA_TERM_TERM_H_ */
