/*
 * Copyright (c) 2025 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _VPU_HW_H_
#define _VPU_HW_H_

/* All pin assignments
 */

#define VPU_LED_PIN 25
#define RGB_LED_PIN 20

// UART connected to RIA
#define COM_UART_TX_PIN 4
#define COM_UART_RX_PIN (COM_UART_TX_PIN + 1)

#define PIX_PIN_BASE  38 /* PIX_D0-7, PIX_CLK, PIX_DTR */
#define PIX_PIN_DTR   (PIX_PIN_BASE + 8)
#define PIX_PIN_RTS   (PIX_PIN_BASE + 9)
#define PIX_PINS_USED 10

#define VPU_NMIB_PIN 21

// ---
#define AUD_SPI_PIN_BASE 32
#define AUD_SPI_RX_PIN   (AUD_SPI_PIN_BASE + 0)
#define AUD_SPI_CS_PIN   (AUD_SPI_PIN_BASE + 1)
#define AUD_SPI_SCK_PIN  (AUD_SPI_PIN_BASE + 2)
#define AUD_SPI_TX_PIN   (AUD_SPI_PIN_BASE + 3)

/* All resource assignments
 */

#define COM_UART_INTERFACE uart1
#define COM_UART_BAUDRATE  115200

// PIX bus
#define PIX_PIO     pio1
#define PIX_SM      0
#define PIX_DMA_IRQ DMA_IRQ_1

// DVI uses DMA in ping-ping setup
#define DVI_DMACH_PING 0
#define DVI_DMACH_PONG 1
#define DVI_DMA_IRQ    DMA_IRQ_0

#define RGB_LED_PIO   pio2
#define RGB_LED_SM    0
#define RGB_LED_COUNT 4

// ---
// FM chip SPI
#define AUD_SPI                 spi0
#define AUD_CLOCK_FREQUENCY_KHZ 12288
#define AUD_BAUDRATE_HZ         1000000

#endif /* _VPU_HW_H_ */
