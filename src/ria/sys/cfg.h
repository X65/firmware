/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _CFG_H_
#define _CFG_H_

#include <stdbool.h>
#include <stdint.h>

/* Kernel events
 */

void cfg_init(void);

// These setters will auto save on change and
// reconfigure the system as necessary.

void cfg_set_boot(char *rom);
char *cfg_get_boot(void);
bool cfg_set_codepage(uint32_t cp);
uint16_t cfg_get_codepage(void);
bool cfg_set_time_zone(const char *pass);
const char *cfg_get_time_zone(void);

#endif /* _CFG_H_ */
