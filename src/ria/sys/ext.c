/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ext.h"

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "main.h"
#include <stdio.h>

uint8_t gpx_read(uint8_t reg)
{
    i2c_write_blocking(EXT_I2C, IOE_I2C_ADDRESS, &reg, 1, true);
    absolute_time_t timeout = make_timeout_time_us(10000); // 0.01 second
    int ret = i2c_read_blocking_until(EXT_I2C, IOE_I2C_ADDRESS, &reg, 1, false, timeout);
    if (ret != 1)
    {
        return (uint8_t)ret;
    }
    return reg;
}

void gpx_dump_registers(void)
{
    printf("\nMCP registers dump\n");
    printf("IODIRA\t\t%02x\n", gpx_read(0x00));
    printf("IODIRB\t\t%02x\n", gpx_read(0x01));
    printf("IPOLA\t\t%02x\n", gpx_read(0x02));
    printf("IPOLB\t\t%02x\n", gpx_read(0x03));
    printf("GPINTENA\t%02x\n", gpx_read(0x04));
    printf("GPINTENB\t%02x\n", gpx_read(0x05));
    printf("DEFVALA\t\t%02x\n", gpx_read(0x06));
    printf("DEFVALB\t\t%02x\n", gpx_read(0x07));
    printf("INTCONA\t\t%02x\n", gpx_read(0x08));
    printf("INTCONB\t\t%02x\n", gpx_read(0x09));
    printf("IOCONA\t\t%02x\n", gpx_read(0x0A));
    printf("IOCONB\t\t%02x\n", gpx_read(0x0B));
    printf("GPPUA\t\t%02x\n", gpx_read(0x0C));
    printf("GPPUB\t\t%02x\n", gpx_read(0x0D));
    printf("INTFA\t\t%02x\n", gpx_read(0x0E));
    printf("INTFB\t\t%02x\n", gpx_read(0x00F));
    printf("INTCAPA\t\t%02x\n", gpx_read(0x10));
    printf("INTCAPB\t\t%02x\n", gpx_read(0x11));
    printf("GPIOA\t\t%02x\n", gpx_read(0x12));
    printf("GPIOB\t\t%02x\n", gpx_read(0x13));
    printf("OLATA\t\t%02x\n", gpx_read(0x14));
    printf("OLATB\t\t%02x\n", gpx_read(0x15));
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
        // Mixer is write-only, but we know it's there
        else if (addr == MIX_I2C_ADDRESS)
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
