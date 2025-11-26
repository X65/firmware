/*
 * Copyright (c) 2025 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _SPU_HW_H_
#define _SPU_HW_H_

/* All pin assignments
 */

#define SPU_LED_PIN 25

#define AUD_I2S_PIN_BASE  44
#define AUD_I2S_DIN_PIN   (AUD_I2S_PIN_BASE + 0)
#define AUD_I2S_SCLK_PIN  (AUD_I2S_PIN_BASE + 1)
#define AUD_I2S_LRCLK_PIN (AUD_I2S_PIN_BASE + 2)
#define AUD_I2S_DOUT_PIN  38 // WARNING: this PIN needs to be in the same GPIO bank as AUD_I2S_PIN_BASE

#define EXT_I2C_SDA_PIN 36
#define EXT_I2C_SCL_PIN 37

/* All resource assignments
 */

// DAC chip I2S
#define AUD_I2S_PIO pio2
#define AUD_I2S_SM  0

// Extension/External 3.3V I2C bus
#define EXT_I2C          i2c0
#define EXT_I2C_BAUDRATE (400 * 1000)

#define IOE_I2C_ADDRESS 0x20 // Address of I/O Extender on I2C bus
#define MIX_I2C_ADDRESS 0x40 // Address of Mixer on I2C bus
#define I2S_I2C_ADDRESS 0x0A // Address of I2S DAC/ADC on I2C bus

#endif /* _SPU_HW_H_ */
