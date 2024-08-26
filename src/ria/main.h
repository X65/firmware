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

#define CPU_RESB_PIN 22
#define CPU_IRQB_PIN 23
#define CPU_NMIB_PIN 11

#define BUS_PIN_BASE       0
#define BUS_DATA_PIN_BASE  (BUS_PIN_BASE + 0) /* D0-D7 */
#define CPU_VAB_PIN        (BUS_PIN_BASE + 8)
#define CPU_RWB_PIN        (BUS_PIN_BASE + 9)
#define BUS_DATA_PINS_USED 10
#define BUS_CTL_PIN_BASE   0
#define CPU_PHI2_PIN       (BUS_CTL_PIN_BASE + 0)
#define BUS_BE0_PIN        (BUS_CTL_PIN_BASE + 1) /* BUF0 ENABLE */
#define BUS_BE1_PIN        (BUS_CTL_PIN_BASE + 2) /* BUF1 ENABLE */
#define BUS_DIR_PIN        (BUS_CTL_PIN_BASE + 3) /* BUFFER2 DIR */
#define BUS_CTL_PINS_USED  4

#define QMI_PSRAM_CS_PIN 20

#define RIA_LED_PIN 25
#define RGB_LED_PIN 26

#define AUD_SPI_PIN_BASE  32
#define AUD_SPI_CLOCK_PIN 34

#define EXT_I2C_SDA_PIN 16
#define EXT_I2C_SCL_PIN 17

/* All resource assignments
 */

#define COM_UART           uart0
#define COM_UART_BAUD_RATE 115200
#define COM_UART_TX_PIN    0
#define COM_UART_RX_PIN    1

// CPU bus handling
#define MEM_BUS_PIO pio1
#define MEM_BUS_SM  0

#define AUD_SPI spi0

#define EXT_I2C i2c0

#define RGB_LED_PIO pio1
#define RGB_LED_SM  2

#endif /* _MAIN_H_ */
