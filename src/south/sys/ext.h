/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _VPU_SYS_EXT_H_
#define _VPU_SYS_EXT_H_

#include <stdint.h>

/* EXTension buses - I2C and GPIO expander
 */

void ext_init(void);

/** API
 */
void ext_bus_scan(void);

// EXTension register read/write
uint8_t ext_reg_read(uint8_t addr, uint8_t reg);
void ext_reg_write(uint8_t addr, uint8_t reg, uint8_t data);

void gpx_dump_registers(void);

#endif /* _VPU_SYS_EXT_H_ */
