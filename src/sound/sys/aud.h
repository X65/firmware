/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _SND_SYS_AUD_H_
#define _SND_SYS_AUD_H_

#include <stddef.h>
#include <stdint.h>

/* Kernel events
 */

void aud_init(void);
void aud_task(void);

void aud_print_status(void);

uint8_t aud_read_fm_register(uint8_t reg);
void aud_write_fm_register(uint8_t reg, uint8_t data);

#endif /* _SND_SYS_AUD_H_ */
