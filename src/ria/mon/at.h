/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RIA_MON_AT_H_
#define _RIA_MON_AT_H_

#include <stddef.h>

/* Monitor commands
 */

void at_mon_at(const char *args, size_t len);

#endif /* _RIA_MON_AT_H_ */
