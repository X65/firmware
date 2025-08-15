/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _AUD_H_
#define _AUD_H_

#include <stddef.h>
#include <stdint.h>

/* Kernel events
 */

void aud_init(void);
void aud_post_reclock(void);
void aud_stop(void);
void aud_task(void);

void aud_print_status(void);

#endif /* _AUD_H_ */
