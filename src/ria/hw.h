/*
 * Copyright (c) 2025 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RIA_HW_H_
#define _RIA_HW_H_

/* All pin assignments
 */

#define RIA_LED_PIN 37

// monitor console
#define COM_UART_TX_PIN    32
#define COM_UART_RX_PIN    (COM_UART_TX_PIN + 1)
#define COM_UART_GPIO_FUNC GPIO_FUNC_UART

#define PIX_PIN_BASE  38 /* PIX0-PIX7, PIX_CLK, PIX_DTR */
#define PIX_PIN_DTR   (PIX_PIN_BASE + 8)
#define PIX_PIN_SCK   (PIX_PIN_BASE + 9)
#define PIX_PINS_USED 10

#define RIA_IRQB_PIN 36 /* connected to IRQ0 on interrupt controller  */

#define PSRAM_BANKS_NO   2
#define QMI_PSRAM_CS_PIN 8
#define QMI_PSRAM_BS_PIN 9

#define CPU_BUS_PIN_BASE  10
#define CPU_DATA_PIN_BASE (CPU_BUS_PIN_BASE + 0) /* D0-D7 */
#define CPU_VAB_PIN       ((CPU_BUS_PIN_BASE + 24) % 32)
#define CPU_RWB_PIN       ((CPU_BUS_PIN_BASE + 25) % 32)
#define CPU_BUS_PINS_USED 26
#define CPU_CTL_PIN_BASE  4
#define CPU_PHI2_PIN      (CPU_CTL_PIN_BASE + 0)
#define CPU_CTL_PINS_USED 1
#define CPU_RESB_PIN      (CPU_CTL_PIN_BASE + CPU_CTL_PINS_USED)

/* All resource assignments
 */

// monitor console UART
#define COM_UART           uart0
#define COM_UART_BAUD_RATE 115200

// CPU bus handling
#define CPU_PHI2_KHZ 6000

#define CPU_BUS_PIO pio0
#define CPU_BUS_SM  0

// PIX bus
#define PIX_PIO pio1
#define PIX_SM  0

#endif /* _RIA_HW_H_ */
