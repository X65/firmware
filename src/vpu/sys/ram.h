/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RAM_H_
#define _RAM_H_

#include "pico/types.h"

void ram_init(void);
void ram_task(void);

void ram_read_blocking(uint32_t addr, uint8_t *dst, uint len);
void ram_write_blocking(uint32_t addr, uint8_t data);
void ram_reg_read_blocking(uint8_t addr, uint8_t die, uint16_t *dst);
void ram_cfgreg_write(uint16_t wdata);

#endif /* _RAM_H_ */
