/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "aud.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
#include "main.h"
#include <stdio.h>

#define READ_BIT 0x80

static uint aud_pwm_a_slice_num;
static uint aud_pwm_a_channel;
static uint aud_pwm_b_slice_num;
static uint aud_pwm_b_channel;

static inline void aud_fm_select(void)
{
    // asm volatile("nop \n nop \n nop");
    gpio_put(AUD_SPI_CS_PIN, 0);
    // asm volatile("nop \n nop \n nop");
}

static inline void aud_fm_deselect(void)
{
    // asm volatile("nop \n nop \n nop");
    gpio_put(AUD_SPI_CS_PIN, 1);
    // asm volatile("nop \n nop \n nop");
}

uint8_t aud_read_fm_register(uint8_t reg)
{
    reg |= READ_BIT;
    uint8_t ret;
    aud_fm_select();
    spi_write_blocking(spi_default, &reg, 1);
    spi_read_blocking(spi_default, 0, &ret, 1);
    aud_fm_deselect();
    return ret;
}

void aud_write_fm_register(uint8_t reg, uint8_t data)
{
    uint8_t buf[2];
    buf[0] = reg & ~READ_BIT;
    buf[1] = data;
    aud_fm_select();
    spi_write_blocking(spi_default, buf, 2);
    aud_fm_deselect();
}

void aud_write_fm_register_multiple(uint8_t reg, uint8_t *data, uint16_t len)
{
    reg &= ~READ_BIT;
    aud_fm_select();
    spi_write_blocking(spi_default, &reg, 1);
    spi_write_blocking(spi_default, data, len);
    aud_fm_deselect();
}

static inline void aud_fm_init(void)
{
    // Generate clock for SD-1 using CLK_GPOUT0
    gpio_set_function(AUD_CLOCK_PIN, GPIO_FUNC_GPCK);

    // Configure SPI communication
    spi_init(AUD_SPI, AUD_BAUDRATE_HZ);
    spi_set_format(AUD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(AUD_SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(AUD_SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(AUD_SPI_TX_PIN, GPIO_FUNC_SPI);
    // Chip select is active-low, so we'll initialise it to a driven-high state
    gpio_init(AUD_SPI_CS_PIN);
    gpio_set_dir(AUD_SPI_CS_PIN, GPIO_OUT);
    gpio_put(AUD_SPI_CS_PIN, 1);
}

static inline void aud_pwm_init(void)
{
    gpio_set_function(BUZZ_PWM_A_PIN, GPIO_FUNC_PWM);
    gpio_set_function(BUZZ_PWM_B_PIN, GPIO_FUNC_PWM);

    aud_pwm_a_slice_num = pwm_gpio_to_slice_num(BUZZ_PWM_A_PIN);
    aud_pwm_a_channel = pwm_gpio_to_channel(BUZZ_PWM_A_PIN);
    aud_pwm_b_slice_num = pwm_gpio_to_slice_num(BUZZ_PWM_B_PIN);
    aud_pwm_b_channel = pwm_gpio_to_channel(BUZZ_PWM_B_PIN);

    // Set the duty cycle to 50%
    pwm_set_wrap(aud_pwm_a_slice_num, 255);
    pwm_set_chan_level(aud_pwm_a_slice_num, aud_pwm_a_channel, 127);
    pwm_set_wrap(aud_pwm_b_slice_num, 255);
    pwm_set_chan_level(aud_pwm_b_slice_num, aud_pwm_b_channel, 127);

    // Disable channels
    pwm_set_enabled(aud_pwm_a_slice_num, false);
    pwm_set_enabled(aud_pwm_b_slice_num, false);
}

void aud_init(void)
{
    aud_fm_init();
    aud_pwm_init();

    // Set clocks
    aud_reclock();
}

static inline void aud_fm_reclock(void)
{
    clock_configure(clk_gpout0,
                    CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC, 0,
                    clock_get_hz(clk_sys),
                    AUD_CLOCK_FREQUENCY_KHZ * 1000);

    spi_set_baudrate(AUD_SPI, AUD_BAUDRATE_HZ);
}

static inline void aud_pwm_reclock(void)
{
    float clock_div = ((float)(clock_get_hz(clk_sys))) / AUD_CLICK_FREQUENCY / 256;
    pwm_set_clkdiv(aud_pwm_a_slice_num, clock_div);
    pwm_set_clkdiv(aud_pwm_b_slice_num, clock_div);
}

void aud_reclock(void)
{
    aud_fm_reclock();
    aud_pwm_reclock();
}

void aud_task(void)
{
    static bool done = false;
    if (!done)
    {
        printf("\nSD-1 registers dump");
        for (uint8_t i = 0; i < 30; ++i)
        {
            ;
            if (i % 10 == 0)
                printf("\n[%02d]", i);
            printf(" %02x", aud_read_fm_register(i));
        }

        done = true;
    }

    // heartbeat
    static bool was_on = false;
    bool on = (time_us_32() / 100000) % AUD_CLICK_DURATION_MS > 8;
    if (was_on != on)
    {
        pwm_set_enabled(aud_pwm_a_slice_num, on);

        was_on = on;
    }
}
