/*
 * Copyright (c) 2023 Rumbledethumps
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _MAIN_H_
#define _MAIN_H_

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
bool main_api(uint8_t operation);

/* Overclock
 */
#define MAIN_VREG_VSEL     VREG_VOLTAGE_1_30
#define MAIN_SYS_CLOCK_KHZ 366000

/* All pin assignments
 */

#define CPU_RESB_PIN 26
#define CPU_IRQB_PIN 10
#define CPU_NMIB_PIN 11

#define BUS_PIN_BASE       0
#define BUS_DATA_PIN_BASE  (BUS_PIN_BASE + 0) /* D0-D7 */
#define CPU_VAB_PIN        (BUS_PIN_BASE + 8)
#define CPU_RWB_PIN        (BUS_PIN_BASE + 9)
#define BUS_DATA_PINS_USED 10
#define BUS_CTL_PIN_BASE   22
#define CPU_PHI2_PIN       (BUS_CTL_PIN_BASE + 0)
#define BUS_BE0_PIN        (BUS_CTL_PIN_BASE + 1) /* BUF0 ENABLE */
#define BUS_BE1_PIN        (BUS_CTL_PIN_BASE + 2) /* BUF1 ENABLE */
#define BUS_DIR_PIN        (BUS_CTL_PIN_BASE + 3) /* BUFFER2 DIR */
#define BUS_CTL_PINS_USED  4

#define QMI_PSRAM_CS_PIN 47
#define QMI_PSRAM_BS_PIN 38

// #define RIA_LED_PIN 25   // not present on X65 board
#define RGB_LED_PIN 45

#define AUD_SPI_PIN_BASE 32
#define AUD_SPI_RX_PIN   (AUD_SPI_PIN_BASE + 0)
#define AUD_SPI_CS_PIN   (AUD_SPI_PIN_BASE + 1)
#define AUD_SPI_SCK_PIN  (AUD_SPI_PIN_BASE + 2)
#define AUD_SPI_TX_PIN   (AUD_SPI_PIN_BASE + 3)
#define AUD_CLOCK_PIN    21 // CLOCK_GPOUT0

#define ESP_SPI_PIN_BASE 40
#define ESP_SPI_RX_PIN   (ESP_SPI_PIN_BASE + 0)
#define ESP_SPI_CS_PIN   (ESP_SPI_PIN_BASE + 1)
#define ESP_SPI_SCK_PIN  (ESP_SPI_PIN_BASE + 2)
#define ESP_SPI_TX_PIN   (ESP_SPI_PIN_BASE + 3)
#define ESP_AT_HS_PIN    39 // SPI HANDSHAKE

#define EXT_I2C_SDA_PIN 36
#define EXT_I2C_SCL_PIN 37

#define AUD_PWM_1_PIN 44 // PWM10_A
#define AUD_PWM_2_PIN 46 // PWM11_A

#define COM_UART_TX_PIN 28
#define COM_UART_RX_PIN 29

#define EXT_IO_CS_PIN  27
#define EXT_IO_BE0_PIN 30
#define EXT_IO_BE1_PIN 31

/* All resource assignments
 */

// DVI uses DMA in ping-ping setup
#define DVI_DMACH_PING 0
#define DVI_DMACH_PONG 1

// monitor console UART (connected to power USB CDC via ESP32)
#define COM_UART           uart0
#define COM_UART_BAUD_RATE 115200

// CPU bus handling
#define MEM_BUS_PIO     pio1
#define MEM_BUS_SM      0
#define MEM_BUS_PIO_IRQ 1 // IRQ0 is used by DVI

// Audio chip SPI
#define AUD_SPI                 spi0
#define AUD_CLOCK_FREQUENCY_KHZ 12288
#define AUD_BAUDRATE_HZ         1000000
// PWM click
#define AUD_PWM_BASE_FREQUENCY  (255 * 256)
#define AUD_CLICK_FREQUENCY     280
#define AUD_CLICK_DUTY          24
#define AUD_CLICK_DURATION_MS   10

// ESP-AT modem SPI
#define ESP_SPI         spi1
#define ESP_BAUDRATE_HZ 10000000

// LEDs
#define RGB_LED_PIO pio1
#define RGB_LED_SM  2

// Extension/External I2C bus (also DVI/HDMI CDC)
#define EXT_I2C          i2c0
#define EXT_I2C_BAUDRATE (400 * 1000)

#define IOE_I2C_ADDRESS 0x20 // Address of I/O Extender on I2C bus
#define MIX_I2C_ADDRESS 0x40 // Address of Mixer on I2C bus

#define PIMORONI_PICO_PLUS_2

#if defined(RASPBERRY_PICO_2)
#undef CPU_RESB_PIN
#define CPU_RESB_PIN 22
#undef CPU_IRQB_PIN
#define CPU_IRQB_PIN 23
#undef BUS_CTL_PIN_BASE
#define BUS_CTL_PIN_BASE 0
#undef QMI_PSRAM_CS_PIN
#define QMI_PSRAM_CS_PIN 20
#define RIA_LED_PIN      25
#undef RGB_LED_PIN
#define RGB_LED_PIN 26
#undef AUD_CLOCK_PIN
#define AUD_CLOCK_PIN 34
#undef EXT_I2C_SDA_PIN
#define EXT_I2C_SDA_PIN 16
#undef EXT_I2C_SCL_PIN
#define EXT_I2C_SCL_PIN 17
#undef COM_UART_TX_PIN
#define COM_UART_TX_PIN 0
#undef COM_UART_RX_PIN
#define COM_UART_RX_PIN 1
#undef ESP_AT_HS_PIN
#define ESP_AT_HS_PIN 11
#elif defined(SPARKFUN_PRO_MICRO)
#undef CPU_RESB_PIN
#define CPU_RESB_PIN 22
#undef CPU_IRQB_PIN
#define CPU_IRQB_PIN 23
#undef BUS_CTL_PIN_BASE
#define BUS_CTL_PIN_BASE 26
#undef QMI_PSRAM_CS_PIN
#define QMI_PSRAM_CS_PIN 19
#undef RGB_LED_PIN
#define RGB_LED_PIN 25
#undef AUD_SPI_PIN_BASE
#define AUD_SPI_PIN_BASE 12
#undef AUD_CLOCK_PIN
#define AUD_CLOCK_PIN 13
#undef ESP_SPI_PIN_BASE
#define ESP_SPI_PIN_BASE 12
#undef ESP_AT_HS_PIN
#define ESP_AT_HS_PIN 18
#undef EXT_I2C_SDA_PIN
#define EXT_I2C_SDA_PIN 16
#undef EXT_I2C_SCL_PIN
#define EXT_I2C_SCL_PIN 17
#undef AUD_PWM_1_PIN
#define AUD_PWM_1_PIN 18
#undef AUD_PWM_2_PIN
#define AUD_PWM_2_PIN 24
#undef COM_UART_TX_PIN
#define COM_UART_TX_PIN 20
#undef COM_UART_RX_PIN
#define COM_UART_RX_PIN 21
#undef COM_UART
#define COM_UART uart1
#elif defined(PIMORONI_PICO_PLUS_2)
#undef CPU_RESB_PIN
#define CPU_RESB_PIN 45
#undef CPU_IRQB_PIN
#define CPU_IRQB_PIN 46
#undef CPU_NMIB_PIN
#define CPU_NMIB_PIN 47
#undef BUS_PIN_BASE
#define BUS_PIN_BASE 30
#undef BUS_CTL_PIN_BASE
#define BUS_CTL_PIN_BASE 40
#define RIA_LED_PIN      25
#undef RGB_LED_PIN
#define RGB_LED_PIN 27
#undef ESP_SPI_PIN_BASE
#define ESP_SPI_PIN_BASE 8
#undef ESP_AT_HS_PIN
#define ESP_AT_HS_PIN 22
#undef EXT_I2C_SDA_PIN
#define EXT_I2C_SDA_PIN 4
#undef EXT_I2C_SCL_PIN
#define EXT_I2C_SCL_PIN 5
#undef AUD_PWM_1_PIN
#define AUD_PWM_1_PIN 26
#undef AUD_PWM_2_PIN
#define AUD_PWM_2_PIN 28
#undef COM_UART_TX_PIN
#define COM_UART_TX_PIN 0
#undef COM_UART_RX_PIN
#define COM_UART_RX_PIN 1
#endif

#endif /* _MAIN_H_ */
