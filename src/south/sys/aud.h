/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _SB_SYS_AUD_H_
#define _SB_SYS_AUD_H_

/* Audio chip communication over SPI
 */

#include <stddef.h>
#include <stdint.h>

/* Main events
 */

void aud_init(void);
void aud_task(void);

void aud_print_status(void);

uint8_t aud_read_register(uint8_t reg);
void aud_write_register(uint8_t reg, uint8_t data);

#endif /* _SB_SYS_AUD_H_ */
