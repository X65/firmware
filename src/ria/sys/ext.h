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
void ext_post_reclock(void);
void ext_stop(void);
void ext_task(void);

/** API
 */
void ext_bus_scan(void);

// EXTension register read/write
uint8_t ext_reg_read(uint8_t addr, uint8_t reg);
void ext_reg_write(uint8_t addr, uint8_t reg, uint8_t data);

void gpx_dump_registers(void);

#endif /* _EXT_H_ */
