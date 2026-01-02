/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _SND_SYS_AUD_H_
#define _SND_SYS_AUD_H_

#include <stddef.h>
#include <stdint.h>

#define AUD_OUT_HZ 44100

/* Kernel events
 */

void aud_init(void);
void aud_task(void);

void aud_print_status(void);

#endif /* _SND_SYS_AUD_H_ */
