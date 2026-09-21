/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _SB_TERM_TERM_H_
#define _SB_TERM_TERM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Main events
 */

void term_init(void);
void term_task(void);

void term_render(int16_t y, uint32_t *rgbbuf);

void term_RIS();
bool term_prog(uint16_t *xregs);

#endif /* _SB_TERM_TERM_H_ */
