/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 a-pushkin on GitHub
 * Copyright (c) 2024 Tomasz Sterna
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "hardware/pio.h"
#include "main.h"
#include <pico/stdlib.h>

#include "led.pio.h"

#define IS_RGBW false

static inline void put_pixel(uint32_t pixel_grb)
{
    pio_sm_put_blocking(RGB_LED_PIO, RGB_LED_SM, pixel_grb << 8u);
}

static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)(r) << 8) | ((uint32_t)(g) << 16) | (uint32_t)(b);
}

void led_init(void)
{
    // LED
    // gpio_init(RIA_LED_PIN);
    // gpio_set_dir(RIA_LED_PIN, GPIO_OUT);
    // gpio_put(RIA_LED_PIN, 1);

    // RGB LED
    uint offset = pio_add_program(RGB_LED_PIO, &ws2812_program);
    ws2812_program_init(RGB_LED_PIO, RGB_LED_SM, offset, RGB_LED_PIN, 800000, IS_RGBW);
}

void led_task(void)
{
    // heartbeat
    static bool was_on = false;
    bool on = (time_us_32() / 100000) % 10 > 8;
    if (was_on != on)
    {
        // LED
        // gpio_put(RIA_LED_PIN, on);

        // RGB LED
        put_pixel(urgb_u32(on ? 0x05 : 0x00, on ? 0x1c : 0x00, on ? 0x26 : 0x00));

        was_on = on;
    }
}
