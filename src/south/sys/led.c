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
#ifdef VPU_LED_PIN
    gpio_init(VPU_LED_PIN);
    gpio_set_dir(VPU_LED_PIN, GPIO_OUT);
    gpio_put(VPU_LED_PIN, on);
#endif
}

static bool rgb_update = false;
static uint32_t RGB_LEDS[RGB_LED_COUNT] = {0};

static inline void put_pixel(uint32_t pixel_grb)
{
    pio_sm_put_blocking(RGB_LED_PIO, RGB_LED_SM, pixel_grb << 8u);
}

static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)(r) << 8) | ((uint32_t)(g) << 16) | (uint32_t)(b);
}

static void led_rgb_init(void)
{
    pio_set_gpio_base(RGB_LED_PIO, RGB_LED_PIN >= 32 ? 16 : 0);

    pio_gpio_init(RGB_LED_PIO, RGB_LED_PIN);
    pio_sm_set_consecutive_pindirs(RGB_LED_PIO, RGB_LED_SM, RGB_LED_PIN, 1, true);

    uint offset = pio_add_program(RGB_LED_PIO, &ws2812_program);
    pio_sm_config c = ws2812_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, RGB_LED_PIN);
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
    rgb_update = true;
}

void led_task(void)
{
    if (rgb_update)
    {
        for (size_t i = 0; i < RGB_LED_COUNT; ++i)
        {
            put_pixel(RGB_LEDS[i]);
        }
        rgb_update = false;
    }

    if (led_blinking && absolute_time_diff_us(get_absolute_time(), led_blink_timer) < 0)
    {
        led_state = !led_state;
        led_set(led_state);
        led_blink_timer = make_timeout_time_ms(LED_BLINK_TIME_MS);
    }
}

void led_blink(bool on)
{
    if (!on)
        led_set(true);
    led_blinking = on;
}

void led_set_pixel(size_t index, uint8_t r, uint8_t g, uint8_t b)
{
    assert(index < RGB_LED_COUNT);
    RGB_LEDS[index] = urgb_u32(r, g, b);
    rgb_update = true;
}
