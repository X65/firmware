/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "./aud.h"
#include "hw.h"

#include <hardware/gpio.h>
#include <hardware/spi.h>
#include <pico/types.h>
#include <stdio.h>

#define SPI_READ_BIT 0x8000

uint8_t aud_read_register(uint8_t reg)
{
    uint16_t packet = (uint16_t)(SPI_READ_BIT | ((uint16_t)(reg & 0x3F) << 8));
    int retries = 10;
    uint16_t response = 0;
    while (retries-- > 0)
    {
        spi_write16_read16_blocking(AUD_SPI, &packet, &response, 1);
        if ((response & 0xFF00) == (packet & 0xFF00))
            return (uint8_t)(response);
    }
    return 0xFF;
}

void aud_write_register(uint8_t reg, uint8_t data)
{
    uint16_t packet = (uint16_t)(((uint16_t)(reg & 0x3F) << 8) | data);
    spi_write16_blocking(AUD_SPI, &packet, 1);
}

void aud_init(void)
{
#ifdef AUD_CLOCK_PIN
    // Generate clock for SGU-1 using CLK_GPOUT0
    // This is used on proto-boards only and is not needed on DEV-board.
    gpio_set_function(AUD_CLOCK_PIN, GPIO_FUNC_GPCK);
    clock_configure(clk_gpout0,
                    CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC, 0,
                    clock_get_hz(clk_sys),
                    AUD_CLOCK_FREQUENCY_KHZ * 1000);
#endif

    // Configure SPI communication
    spi_init(AUD_SPI, AUD_BAUDRATE_HZ);
    spi_set_format(AUD_SPI, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(AUD_SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(AUD_SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(AUD_SPI_TX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(AUD_SPI_CS_PIN, GPIO_FUNC_SPI);
}

static void aud_dump_registers(void)
{
    printf("\nSD-1 .0 .1 .2 .3 .4 .5 .6 .7 .8 .9 .A .B .C .D .E .F");
    for (uint8_t i = 0; i <= 80; ++i)
    {
        ;
        if (i % 0x10 == 0)
            printf("\n[%02X]", i);
        printf(" %02X", aud_read_register(i));
    }
}

void aud_task(void)
{
}

void aud_print_status(void)
{
    // aud_dump_registers();
}
