/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _AUD_H_
#define _AUD_H_

/* Kernel events
 */

void aud_init(void);
void aud_reclock(void);
void aud_stop(void);
void aud_task(void);

#endif /* _AUD_H_ */
