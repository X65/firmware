/*
 * Copyright (c) 2025 Rumbledethumps
 * Copyright (c) 2021 a-pushkin on GitHub
 * Copyright (c) 2025 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/led.h"
#include "hw.h"
#include "rgb.pio.h"
#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <pico/stdlib.h>

#if defined(DEBUG_RIA_SYS) || defined(DEBUG_RIA_SYS_LED)
#include <stdio.h>
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
static inline void DBG(const char *fmt, ...)
{
    (void)fmt;
}
#endif

#define LED_BLINK_TIME_MS 100

bool led_state;
bool led_blinking;
absolute_time_t led_blink_timer;

static void led_set(bool on)
{
    led_state = on;
#ifdef SND_LED_PIN
    gpio_init(SND_LED_PIN);
    gpio_set_dir(SND_LED_PIN, GPIO_OUT);
    gpio_put(SND_LED_PIN, on);
#endif
}

void led_put(uint32_t grb)
{
    pio_sm_put_blocking(RGB_LED_PIO, RGB_LED_SM, grb << 8u);
}

static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)(r) << 8) | ((uint32_t)(g) << 16) | (uint32_t)(b);
}

static void led_rgb_init(void)
{
    pio_set_gpio_base(RGB_LED_PIO, SND_RGB_LED_PIN >= 32 ? 16 : 0);

    pio_gpio_init(RGB_LED_PIO, SND_RGB_LED_PIN);
    gpio_pull_down(SND_RGB_LED_PIN); // ensure low during indexing reset pause
    pio_sm_set_consecutive_pindirs(RGB_LED_PIO, RGB_LED_SM, SND_RGB_LED_PIN, 1, true);

    pio_sm_claim(RGB_LED_PIO, RGB_LED_SM);
    uint offset = pio_add_program(RGB_LED_PIO, &ws2812_program);
    pio_sm_config c = ws2812_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, SND_RGB_LED_PIN);
    sm_config_set_out_shift(&c, false, true, 24);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    float div = (float)(clock_get_hz(clk_sys)) / ws2812_FREQ;
    sm_config_set_clkdiv(&c, div);

    pio_sm_init(RGB_LED_PIO, RGB_LED_SM, offset, &c);
    pio_sm_set_enabled(RGB_LED_PIO, RGB_LED_SM, true);
}

void led_init(void)
{
    led_set(true);

    led_rgb_init();
}

void led_task(void)
{
    if (led_blinking && absolute_time_diff_us(get_absolute_time(), led_blink_timer) < 0)
    {
        led_state = !led_state;
        led_set(led_state);
        led_put(led_state ? 0x220000 : 0);
        led_blink_timer = make_timeout_time_ms(LED_BLINK_TIME_MS);
    }
}

void led_blink(bool on)
{
    if (!on)
        led_set(true);
    led_blinking = on;
}
