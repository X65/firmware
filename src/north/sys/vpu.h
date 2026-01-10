/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RIA_SYS_VPU_H_
#define _RIA_SYS_VPU_H_

/* Communications with RP6502-VGA.
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

// Responders for status.
int vpu_boot_response(char *buf, size_t buf_size, int state);
int vpu_status_response(char *buf, size_t buf_size, int state);

#endif /* _RIA_SYS_VPU_H_ */
