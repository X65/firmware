/*
 * Copyright (c) 2023 Rumbledethumps
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _TERM_H_
#define _TERM_H_

#include <sys/types.h>

void term_init(void);
void term_task(void);
void term_render(uint y, uint32_t *rgbbuf);

#endif /* _TERM_H_ */
