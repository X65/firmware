/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ext.h"

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "main.h"

void ext_init(void)
{
    i2c_init(EXT_I2C, EXT_I2C_BAUDRATE);
    gpio_set_function(EXT_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(EXT_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(EXT_I2C_SDA_PIN);
    gpio_pull_up(EXT_I2C_SCL_PIN);

    // Set clocks
    ext_reclock();
}

void ext_reclock(void)
{
    i2c_set_baudrate(EXT_I2C, EXT_I2C_BAUDRATE);
}

void ext_stop(void)
{
}

void ext_task(void)
{
}
