/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _BUS_H_
#define _BUS_H_

/* Kernel events
 */

void bus_init(void);
void bus_task(void);
void bus_run(void);
void bus_stop(void);

void bus_print_status(void);

#endif /* _BUS_H_ */
