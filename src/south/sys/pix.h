/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _SB_SYS_PIX_H_
#define _SB_SYS_PIX_H_

/* PIX bus
 */

#include "../pix.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Main events
 */

void pix_init(void);
void pix_task(void);

#endif /* _SB_SYS_PIX_H_ */
