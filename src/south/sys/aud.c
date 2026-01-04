/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "./aud.h"
#include "hw.h"

#include <hardware/gpio.h>
#include <hardware/spi.h>
#include <stdio.h>

#define SGU1_SPI_READ_BIT 0x80 // Read/Write command bit on SGU-1 bus

static inline void aud_select(void)
{
    // asm volatile("nop \n nop \n nop");
    gpio_put(AUD_SPI_CS_PIN, 0);
    // asm volatile("nop \n nop \n nop");
}

static inline void aud_deselect(void)
{
    // asm volatile("nop \n nop \n nop");
    gpio_put(AUD_SPI_CS_PIN, 1);
    // asm volatile("nop \n nop \n nop");
}

uint8_t aud_read_register(uint8_t reg)
{
    reg |= SGU1_SPI_READ_BIT;
    uint8_t ret;
    aud_select();
    spi_write_blocking(spi_default, &reg, 1);
    spi_read_blocking(spi_default, 0, &ret, 1);
    aud_deselect();
    return ret;
}

void aud_write_register(uint8_t reg, uint8_t data)
{
    uint8_t buf[2];
    buf[0] = reg & ~SGU1_SPI_READ_BIT;
    buf[1] = data;
    aud_select();
    spi_write_blocking(spi_default, buf, 2);
    aud_deselect();
}

static void aud_write_register_multiple(uint8_t reg, uint8_t *data, uint16_t len)
{
    reg &= ~SGU1_SPI_READ_BIT;
    aud_select();
    spi_write_blocking(spi_default, &reg, 1);
    spi_write_blocking(spi_default, data, len);
    aud_deselect();
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
    spi_set_baudrate(AUD_SPI, AUD_BAUDRATE_HZ);
    gpio_set_function(AUD_SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(AUD_SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(AUD_SPI_TX_PIN, GPIO_FUNC_SPI);
    // Chip select is active-low, so we'll initialise it to a driven-high state
    gpio_init(AUD_SPI_CS_PIN);
    gpio_set_dir(AUD_SPI_CS_PIN, GPIO_OUT);
    gpio_set_pulls(AUD_SPI_CS_PIN, false, false);
    gpio_put(AUD_SPI_CS_PIN, 1);
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
