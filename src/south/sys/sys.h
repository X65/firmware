/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _SB_SYS_SYS_H_
#define _SB_SYS_SYS_H_

/* System Information
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// SB version string
const char *sys_version(void);

void sys_write_status(void);

#endif /* _SB_SYS_SYS_H_ */
