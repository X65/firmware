/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "buz.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/timer.h"
#include "main.h"

static uint slice_num;
static uint channel;

void buz_init(void)
{
    gpio_set_function(BUZZ_PWM_A_PIN, GPIO_FUNC_PWM);

    slice_num = pwm_gpio_to_slice_num(BUZZ_PWM_A_PIN);
    channel = pwm_gpio_to_channel(BUZZ_PWM_A_PIN);

    // Set the duty cycle to 50%
    pwm_set_wrap(slice_num, 255);
    pwm_set_chan_level(slice_num, channel, 127);

    buz_reclock();
}

void buz_reclock(void)
{
    float clock_div = ((float)(clock_get_hz(clk_sys))) / BUZZ_CLICK_FREQUENCY / 256;
    pwm_set_clkdiv(slice_num, clock_div);
}

void buz_task(void)
{
    // heartbeat
    static bool was_on = false;
    bool on = (time_us_32() / 100000) % BUZZ_CLICK_DURATION_MS > 8;
    if (was_on != on)
    {
        pwm_set_enabled(slice_num, on);

        was_on = on;
    }
}
