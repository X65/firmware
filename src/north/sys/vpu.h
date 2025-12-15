/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RIA_SYS_VPU_H_
#define _RIA_SYS_VPU_H_

/* Communications with VPU (inside South Bridge RP2350).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Main events
 */

void vpu_init(void);
void vpu_task(void);
void vpu_run(void);
void vpu_stop(void);
void vpu_break(void);

// Global VPU status
extern uint16_t vpu_raster;

// For monitor status command.
void vpu_print_status(void);
void vpu_fetch_status(void);

#endif /* _RIA_SYS_VPU_H_ */
