/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _TERM_H_
#define _TERM_H_

#include "pico/types.h"

void term_init(void);
void term_task(void);
void term_clear(void);
void term_render(uint y, int plane, uint32_t *tmdsbuf);

#endif /* _TERM_H_ */
