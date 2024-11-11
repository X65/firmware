/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _OUT_H_
#define _OUT_H_

#define FRAME_WIDTH  768
#define FRAME_HEIGHT 240

void out_init(void);
void out_reclock(void);
void out_print_status(void);

#endif /* _OUT_H_ */
