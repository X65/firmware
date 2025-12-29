/*
 * Copyright (c) 2025 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _SND_HW_H_
#define _SND_HW_H_

/* All pin assignments
 */

#define SND_RGB_LED_PIN 25

#define COM_UART_TX_PIN 0
#define COM_UART_RX_PIN (COM_UART_TX_PIN + 1)

#define AUD_SPI_PIN_BASE 20
#define AUD_SPI_RX_PIN   (AUD_SPI_PIN_BASE + 0)
#define AUD_SPI_CS_PIN   (AUD_SPI_PIN_BASE + 1)
#define AUD_SPI_SCK_PIN  (AUD_SPI_PIN_BASE + 2)
#define AUD_SPI_TX_PIN   (AUD_SPI_PIN_BASE + 3)

#define AUD_I2S_PIN_BASE  44
#define AUD_I2S_DIN_PIN   (AUD_I2S_PIN_BASE + 0)
#define AUD_I2S_SCLK_PIN  (AUD_I2S_PIN_BASE + 1)
#define AUD_I2S_LRCLK_PIN (AUD_I2S_PIN_BASE + 2)
#define AUD_I2S_DOUT_PIN  38 // WARNING: this PIN must be in the same GPIO bank as AUD_I2S_PIN_BASE

#define EXT_I2C_SDA_PIN 16
#define EXT_I2C_SCL_PIN 17

/* All resource assignments
 */

#define COM_UART_INTERFACE uart0
#define COM_UART_BAUDRATE  115200

// DAC chip I2S
#define AUD_I2S_PIO pio2
#define AUD_I2S_SM  0

// CODEC I2C bus
#define EXT_I2C          i2c0
#define EXT_I2C_BAUDRATE (400 * 1000)

#define MIX_I2C_ADDRESS 0x40 // Address of Mixer on I2C bus
#define I2S_I2C_ADDRESS 0x0A // Address of I2S DAC/ADC on I2C bus

#endif /* _SND_HW_H_ */
