/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RIA_H_
#define _RIA_H_

/* RP816 Interface Adapter for WDC W65C816S.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Kernel events
 */

void ria_init(void);
void ria_task(void);
void ria_run();
void ria_stop();

// The RIA is active when it's performing an mbuf action.
bool ria_active();

// Compute CRC32 of mbuf to match zlib.
uint32_t ria_buf_crc32();

#endif /* _RIA_H_ */
