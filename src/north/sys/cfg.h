/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RIA_SYS_CFG_H_
#define _CF_RIA_SYS_CFG_H_G_H_

/* System configuration manager.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Main events
 */

void cfg_init(void);

// These setters will auto save on change and
// reconfigure the system as necessary.

void cfg_set_boot(char *rom);
char *cfg_get_boot(void);
bool cfg_set_code_page(uint32_t cp);
uint16_t cfg_get_code_page(void);
bool cfg_set_time_zone(const char *pass);
const char *cfg_get_time_zone(void);

#endif /* _RIA_SYS_CFG_H_ */
