/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _MAIN_H_
#define _MAIN_H_

#include "hardware/irq.h"
#include <stdbool.h>
#include <stdint.h>

/* This is the main kernel event loop.
 */

// Request to "start the CPU".
// It will safely do nothing if the CPU is already running.
void main_run(void);

// Request to "stop the CPU".
// It will safely do nothing if the CPU is already stopped.
void main_stop(void);

// Request to "break the system".
// A break is triggered by CTRL-ALT-DEL and UART breaks.
// If the CPU is running, stop events will be called first.
// Kernel modules should reset to a state similar to after
// init() was first run.
void main_break(void);

// This is true when the CPU is running or there's a pending
// request to start it.
bool main_active(void);

/* Special events dispatched in main.c
 */

void main_task(void);
void main_reclock(void);

/* All pin assignments
 */

#define RIA_LED_PIN 15

#define CPU_RESB_PIN 26
#define CPU_IRQB_PIN 10
#define CPU_NMIB_PIN 11

#define MEM_BUS_PIN_BASE  0
#define MEM_DATA_PIN_BASE (MEM_BUS_PIN_BASE + 0) /* D0-D7 */
#define CPU_VAB_PIN       (MEM_BUS_PIN_BASE + 8)
#define CPU_RWB_PIN       (MEM_BUS_PIN_BASE + 9)
#define MEM_BUS_PINS_USED 10
#define MEM_CTL_PIN_BASE  22
#define CPU_PHI2_PIN      (MEM_CTL_PIN_BASE + 0)
#define MEM_BE0_PIN       (MEM_CTL_PIN_BASE + 1) /* BUF0 ENABLE */
#define MEM_BE1_PIN       (MEM_CTL_PIN_BASE + 2) /* BUF1 ENABLE */
#define MEM_DATA_DIR_PIN  (MEM_CTL_PIN_BASE + 3) /* BUFFER2 DIR */
#define MEM_CTL_PINS_USED 4

#define AUD_SPI_PIN_BASE  32
#define AUD_SPI_CLOCK_PIN 34

#define EXT_I2C_SDA_PIN 20
#define EXT_I2C_SCL_PIN 21

/* All resource assignments
 */

#define COM_UART           uart0
#define COM_UART_BAUD_RATE 115200
#define COM_UART_TX_PIN    30
#define COM_UART_RX_PIN    31

// CPU bus handling
#define MEM_BUS_PIO pio0
#define MEM_BUS_SM  0

#define AUD_SPI spi0

#define EXT_I2C i2c0

#endif /* _MAIN_H_ */
