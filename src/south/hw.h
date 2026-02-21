/*
 * Copyright (c) 2025 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _VPU_HW_H_
#define _VPU_HW_H_

/* All pin assignments

- 0-2: Audio I2S (DIN, SCLK, LRCLK)
- 3: unused
- 4-5: UART
- 6-7: unused
- 8-10: DVI (I2C + CEC)
- 11: unused
- 12-19: DVI HSTX (D0-2, CK, each with +/-)
- 20: VPU NMIB
- 21: unused
- 22-25: Ext IO enable 0-4
- 25: LED (only on PICO2-BB48 prototype board, not on final VPU)
- 26-27: Ext I2C
- 28-29: unused
- 30: Buzzer PWM
- 31: RGB LED
- 32-35: Audio SPI (RX, CS, SCK, TX)
- 36-37: unused
- 38-47: PIX bus (D0-7, DTR, RTS)
 */

#define VPU_LED_PIN 25
#define RGB_LED_PIN 31

// UART connected to RIA
#define COM_UART_TX_PIN 4
#define COM_UART_RX_PIN (COM_UART_TX_PIN + 1)

#define PIX_PIN_BASE  38 /* PIX_D0-7, ... */
#define PIX_PIN_DTR   (PIX_PIN_BASE + 8)
#define PIX_PIN_RTS   (PIX_PIN_BASE + 9)
#define PIX_PINS_USED 10

#define VPU_NMIB_PIN 20

#define IO0_EN_PIN 22
#define IO1_EN_PIN 23
#define IO2_EN_PIN 24
#define IO3_EN_PIN 25

#define BUZZ_PWM_A_PIN 30
#define BUZZ_PWM_B_PIN (BUZZ_PWM_A_PIN + 1)

#define DVI_SDA_PIN 8
#define DVI_SCL_PIN 9
#define DVI_CEC_PIN 10

#define EXT_I2C_SDA_PIN 26
#define EXT_I2C_SCL_PIN 27

#define AUD_SPI_PIN_BASE 32
#define AUD_SPI_RX_PIN   (AUD_SPI_PIN_BASE + 0)
#define AUD_SPI_CS_PIN   (AUD_SPI_PIN_BASE + 1)
#define AUD_SPI_SCK_PIN  (AUD_SPI_PIN_BASE + 2)
#define AUD_SPI_TX_PIN   (AUD_SPI_PIN_BASE + 3)

#define AUD_I2S_PIN_BASE  0
#define AUD_I2S_DIN_PIN   (AUD_I2S_PIN_BASE + 0)
#define AUD_I2S_SCLK_PIN  (AUD_I2S_PIN_BASE + 1)
#define AUD_I2S_LRCLK_PIN (AUD_I2S_PIN_BASE + 2)

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
#define RGB_LED_SM    2
#define RGB_LED_COUNT 4
#define RGB_LED_MAX   256

#define BUZ_CLICK_FREQUENCY   280
#define BUZ_CLICK_DUTY        24
#define BUZ_CLICK_DURATION_MS 10

#define DVI_I2C i2c0

// Extension/External 3.3V I2C bus
#define EXT_I2C          i2c1
#define EXT_I2C_BAUDRATE (400 * 1000)

#define IOE_I2C_ADDRESS 0x20 // Address of I/O Extender on I2C bus

#define AUD_SPI         spi0
#define AUD_BAUDRATE_HZ 1000000

// DAC chip I2S
#define AUD_I2S_PIO pio2
#define AUD_I2S_SM  0

#endif /* _VPU_HW_H_ */
