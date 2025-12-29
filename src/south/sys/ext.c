/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ext.h"

#include "hw.h"
#include <hardware/gpio.h>
#include <hardware/i2c.h>
#include <stdio.h>

#define EXT_I2C_OPERATION_TIMEOUT_US 1000 // 1 ms

void ext_reg_write(uint8_t addr, uint8_t reg, uint8_t data)
{
    uint8_t buf[2];
    buf[0] = reg;
    buf[1] = data;
    int ret = i2c_write_blocking_until(EXT_I2C, addr, buf, 2, false, make_timeout_time_ms(EXT_I2C_OPERATION_TIMEOUT_US));
    if (ret != 2)
    {
        // printf("Error writing I2C register %02x with %02x: %d\n", reg, data, ret);
        return;
    }
}
uint8_t ext_reg_read(uint8_t addr, uint8_t reg)
{
    int ret = i2c_write_blocking(EXT_I2C, addr, &reg, 1, true);
    if (ret != 1)
    {
        // printf("Error reading I2C register %02x (write): %d\n", reg, ret);
        return (uint8_t)ret;
    }

    ret = i2c_read_blocking_until(EXT_I2C, addr, &reg, 1, false, make_timeout_time_us(EXT_I2C_OPERATION_TIMEOUT_US));
    if (ret != 1)
    {
        // printf("Error reading I2C register %02x (read): %d\n", reg, ret);
        return (uint8_t)ret;
    }
    return reg;
}

void gpx_dump_registers(void)
{
    printf("\nI/O Expander registers dump\n");
    printf("Input Port 0\t\t%02x\n", ext_reg_read(IOE_I2C_ADDRESS, 0x00));
    printf("Input Port 1\t\t%02x\n", ext_reg_read(IOE_I2C_ADDRESS, 0x01));
    printf("Output Port 0\t\t%02x\n", ext_reg_read(IOE_I2C_ADDRESS, 0x02));
    printf("Output Port 1\t\t%02x\n", ext_reg_read(IOE_I2C_ADDRESS, 0x03));
    printf("Polarity Inversion 0\t%02x\n", ext_reg_read(IOE_I2C_ADDRESS, 0x04));
    printf("Polarity Inversion 1\t%02x\n", ext_reg_read(IOE_I2C_ADDRESS, 0x05));
    printf("Configuration 0\t\t%02x\n", ext_reg_read(IOE_I2C_ADDRESS, 0x06));
    printf("Configuration 1\t\t%02x\n", ext_reg_read(IOE_I2C_ADDRESS, 0x07));
}

// I2C reserves some addresses for special purposes. We exclude these from the scan.
// These are any addresses of the form 000 0xxx or 111 1xxx
static bool reserved_addr(uint8_t addr)
{
    return (addr & 0x78) == 0 || (addr & 0x78) == 0x78;
}

void ext_bus_scan(void)
{
    printf("\nI2C Bus Scan\n");
    printf("   0 1 2 3 4 5 6 7 8 9 A B C D E F\n");

    for (uint8_t addr = 0; addr < (1 << 7); ++addr)
    {
        if (addr % 16 == 0)
        {
            printf("%02x ", addr);
        }

        // Perform a 1-byte dummy read from the probe address. If a slave
        // acknowledges this address, the function returns the number of bytes
        // transferred. If the address byte is ignored, the function returns
        // -1.

        int ret;
        uint8_t rxdata;
        // Skip over any reserved addresses.
        if (reserved_addr(addr))
            ret = PICO_ERROR_GENERIC;
        // GPIO extender is not answering in RESET, so fake it
        else if (addr == IOE_I2C_ADDRESS)
            ret = 0xff;
        else
            ret = i2c_read_blocking_until(EXT_I2C, addr, &rxdata, 1, false, make_timeout_time_ms(500));

        printf(ret < 0 ? "." : "@");
        printf(addr % 16 == 15 ? "\n" : " ");
    }
}

void ext_init(void)
{
    i2c_init(EXT_I2C, EXT_I2C_BAUDRATE);

    gpio_set_function(EXT_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(EXT_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_set_pulls(EXT_I2C_SDA_PIN, true, false);
    gpio_set_pulls(EXT_I2C_SCL_PIN, true, false);
    gpio_set_drive_strength(EXT_I2C_SDA_PIN, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(EXT_I2C_SCL_PIN, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_slew_rate(EXT_I2C_SDA_PIN, GPIO_SLEW_RATE_FAST);
    gpio_set_slew_rate(EXT_I2C_SCL_PIN, GPIO_SLEW_RATE_FAST);

    // Set clocks
    i2c_set_baudrate(EXT_I2C, EXT_I2C_BAUDRATE);
}
