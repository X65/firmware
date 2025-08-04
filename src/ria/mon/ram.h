/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RAM_H_
#define _RAM_H_

#include <stdbool.h>
#include <stddef.h>

/* Kernel events
 */

bool ram_active(void);

/* Monitor commands
 */

void ram_mon_binary(const char *args, size_t len);
void ram_mon_address(const char *args, size_t len);
void ram_mon_test(const char *args, size_t len);

#endif /* _RAM_H_ */
