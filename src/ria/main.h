/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _MAIN_H_
#define _MAIN_H_

#include <stdbool.h>
#include <stdint.h>

/* This is the main kernel event loop.
 */

// Request to "start the 6502".
// It will safely do nothing if the 6502 is already running.
void main_run(void);

// Request to "stop the 6502".
// It will safely do nothing if the 6502 is already stopped.
void main_stop(void);

// Request to "break the system".
// A break is triggered by CTRL-ALT-DEL and UART breaks.
// If the 6502 is running, stop events will be called first.
// Kernel modules should reset to a state similar to after
// init() was first run.
void main_break(void);

// This is true when the 6502 is running or there's a pending
// request to start it.
bool main_active(void);

/* Special events dispatched in main.c
 */

void main_task(void);

/* All pin assignments
 */

#define CPU_RESB_PIN 29
#define CPU_IRQB_PIN 28

/* All resource assignments
 */

#define COM_UART           uart1
#define COM_UART_BAUD_RATE 115200
#define COM_UART_TX_PIN    4
#define COM_UART_RX_PIN    5

#define MEM_BANKS 4

#endif /* _MAIN_H_ */
