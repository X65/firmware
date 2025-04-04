/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _CPU_H_
#define _CPU_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 1-byte message queue to the RIA action loop.
// -1 when invalid
extern volatile int cpu_rx_char;

/* Kernel events
 */

void cpu_init(void);
void cpu_task(void);
void cpu_run(void);
void cpu_stop(void);
void cpu_reclock(void);
void cpu_api_phi2(void);

// The CPU is active when RESB is high or when
// we're waiting for the RESB timer.
bool cpu_active(void);

/* Config handlers
 */

uint32_t cpu_validate_phi2_khz(uint32_t freq_khz);
bool cpu_set_phi2_khz(uint32_t freq_khz);

// Return calculated reset time. May be higher than requested
// to guarantee the CPU gets two clock cycles during reset.
uint32_t cpu_get_reset_us();

// Receive UART and keyboard communications intended for the CPU.
void cpu_com_rx(uint8_t ch);

// Get char from CPU rx buf
uint8_t cpu_getchar(void);

#endif /* _CPU_H_ */
