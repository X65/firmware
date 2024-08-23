/*
 * Copyright (c) 2023 Rumbledethumps
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RIA_H_
#define _RIA_H_

/* RP816 Interface Adapter for WDC W65C816S.
 */

#include <stdbool.h>
#include <stddef.h>

/* Kernel events
 */

void ria_init(void);
void ria_task(void);
void ria_run(void);
void ria_stop(void);

#endif /* _RIA_H_ */
