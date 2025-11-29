/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _SB_TERM_FONT_H_
#define _SB_TERM_FONT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern uint8_t font8[2048];
extern uint8_t font16[4096];

/* Main events
 */

void font_init(void);

void font_set_code_page(uint16_t cp);

#endif /* _SB_TERM_FONT_H_ */
