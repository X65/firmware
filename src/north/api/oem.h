/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RIA_API_OEM_H_
#define _RIA_API_OEM_H_

/* The OEM driver manages IBM/DOS style code pages.
 * The affects X65-VPU, FatFs, and keyboards.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Main events
 */

void oem_init(void);
void oem_stop(void);

// Attempt to change the code page.
// On failure, preserve current value if possible.
// Use default as a last resort.
uint16_t oem_set_code_page(uint16_t cp);

// API set or query the code page.
bool oem_api_code_page(void);
// Pull the character generator for the current code page.
bool oem_api_get_chargen(void);

#endif /* _RIA_API_OEM_H_ */
