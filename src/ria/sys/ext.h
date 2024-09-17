/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _EXT_H_
#define _EXT_H_

#include <stdint.h>

/* Kernel events
 */

void ext_init(void);
void ext_reclock(void);
void ext_stop(void);
void ext_task(void);

/** API
 */
void ext_bus_scan(void);

// GPIO EXTender
uint8_t gpx_read(uint8_t reg);
void gpx_dump_registers(void);

#endif /* _EXT_H_ */
