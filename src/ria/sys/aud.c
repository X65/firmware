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

#define ARRAY_SIZE(x) ((sizeof x) / (sizeof *x))

#define READ_BIT 0x80

static struct
{
    uint slice_num;
    uint channel;
    uint gpio;
    uint16_t frequency;
    uint8_t duty;
} pwm_channels[2];

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

static void aud_pwm_init_channel(size_t channel, uint gpio)
{
    pwm_channels[channel].gpio = gpio;
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    pwm_channels[channel].slice_num = pwm_gpio_to_slice_num(gpio);
    pwm_channels[channel].channel = pwm_gpio_to_channel(gpio);
    pwm_channels[channel].frequency = 0;
    pwm_channels[channel].duty = 0;

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&cfg, PWM_DIV_FREE_RUNNING);
    pwm_config_set_phase_correct(&cfg, true);
    pwm_init(pwm_channels[channel].slice_num, &cfg, true);
}

static void aud_pwm_set_channel(size_t channel, uint16_t freq, uint8_t duty)
{
    float clock_div = 0;
    int wrap_shift = 0;
    if (freq == 0)
    {
        duty = 0;
    }
    else
    {
        float clock_div_base = (float)(clock_get_hz(clk_sys)) / freq;
        do
        {
            clock_div = clock_div_base / (float)((UINT8_MAX + 1) << wrap_shift) / 2;
        } while (clock_div > 256.f && wrap_shift++ < 9);

        if (wrap_shift > 8)
        {
            printf("? cannot handle channel %d frequency: %d\n", channel, freq);
        }
    }

    pwm_channels[channel].frequency = freq;
    pwm_channels[channel].duty = duty;
    if (clock_div > 0)
    {
        pwm_set_clkdiv(pwm_channels[channel].slice_num, clock_div);
        pwm_set_wrap(pwm_channels[channel].slice_num, (uint16_t)(UINT8_MAX << wrap_shift));
    }
    pwm_set_chan_level(pwm_channels[channel].slice_num,
                       pwm_channels[channel].channel, (uint16_t)(duty << wrap_shift));
}

static inline void aud_pwm_init(void)
{
    aud_pwm_init_channel(0, BUZZ_PWM_A_PIN);
    aud_pwm_init_channel(1, BUZZ_PWM_B_PIN);
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
    for (size_t i = 0; i < ARRAY_SIZE(pwm_channels); ++i)
    {
        aud_pwm_set_channel(i, (uint16_t)pwm_channels[i].channel, pwm_channels[i].duty);
    }
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
        aud_pwm_set_channel(0, on ? AUD_CLICK_FREQUENCY : 0, AUD_CLICK_DUTY);
        was_on = on;
    }
}
