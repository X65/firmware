/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _EXT_H_
#define _EXT_H_

/* Kernel events
 */

void ext_init(void);
void ext_reclock(void);
void ext_stop(void);
void ext_task(void);

#endif /* _EXT_H_ */
