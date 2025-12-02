/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RIA_SYS_CPU_H_
#define _RIA_SYS_CPU_H_

/* Driver for the 6502.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CPU_BUS_PIO_SPEED_KHZ 10000U

/* Main events
 */

void cpu_init(void);
void cpu_task(void);
void cpu_run(void);
void cpu_stop(void);

// The CPU is active when RESB is high or when
// we're waiting for the RESB timer.
bool cpu_active(void);

void cpu_print_status(void);

/* Config handlers
 */

// Return calculated reset time. May be higher than configured
// to guarantee the CPU gets two clock cycles during reset.
uint32_t cpu_get_reset_us();

#endif /* _RIA_SYS_CPU_H_ */
