/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RIA_H_
#define _RIA_H_

#include <stdint.h>
#include <stdbool.h>

void ria_init(void);
void ria_reclock(void);
void ria_backchan(uint16_t word);
void ria_stdout_rx(char ch);
bool ria_stdout_is_readable();
char ria_stdout_getc();
void ria_vsync(void);

#endif /* _RIA_H_ */
