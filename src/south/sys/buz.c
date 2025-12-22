/*
 * Copyright (c) 2025 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "buz.h"

#include "hw.h"
#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/pwm.h>
#include <hardware/timer.h>
#include <stdio.h>

static uint slice_num;
static uint channel;

static uint16_t frequency;
static uint8_t duty_cycle;
static uint8_t wrap_shift;

void buz_init(void)
{
    gpio_set_function(BUZZ_PWM_A_PIN, GPIO_FUNC_PWM);

    slice_num = pwm_gpio_to_slice_num(BUZZ_PWM_A_PIN);
    channel = pwm_gpio_to_channel(BUZZ_PWM_A_PIN);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&cfg, PWM_DIV_FREE_RUNNING);
    pwm_config_set_phase_correct(&cfg, true);
    pwm_init(slice_num, &cfg, true);

    buz_set_freq_duty(0, 0);
}

void buz_set_freq_duty(uint16_t freq, uint8_t duty)
{
    float clock_div = 0;
    int wrap_sh = 0;
    if (freq == 0)
    {
        duty = 0;
    }
    else
    {
        float clock_div_base = (float)(clock_get_hz(clk_sys)) / freq;
        do
        {
            clock_div = clock_div_base / (float)((UINT8_MAX + 1) << wrap_sh) / 2;
        } while (clock_div > 256.f && wrap_sh++ < 9);

        if (wrap_sh > 8)
        {
            printf("? cannot handle channel %d frequency: %d\n", channel, freq);
        }
    }

    frequency = freq;
    duty_cycle = duty;
    wrap_shift = (uint8_t)wrap_sh;
    if (clock_div > 0)
    {
        pwm_set_clkdiv(slice_num, clock_div);
        pwm_set_wrap(slice_num, (uint16_t)(UINT8_MAX << wrap_shift));
    }
    pwm_set_chan_level(slice_num,
                       channel, (uint16_t)(duty_cycle << wrap_shift));
}

static void buz_set_duty(uint8_t duty)
{
    duty_cycle = duty;
    pwm_set_chan_level(slice_num,
                       channel, (uint16_t)(duty_cycle << wrap_shift));
}

#define ARRAY_SIZE(x) ((sizeof x) / (sizeof *x))

#include "../../../.././examples/src/cgia/data/audio_data.h"
static size_t audio_data_offset = 0;
#define AUD_PWM_BASE_FREQUENCY 40000

void buz_task(void)
{
    // // heartbeat
    // static bool was_on = false;
    // bool on = (time_us_32() / 100000) % BUZ_CLICK_DURATION_MS > 8;
    // if (was_on != on)
    // {
    //     buz_set_freq_duty(on ? BUZ_CLICK_FREQUENCY : 0, BUZ_CLICK_DUTY);
    //     was_on = on;
    // }

    // play sampled music
    static uint32_t next_time = 0;
    if (next_time == 0)
    {
        next_time = time_us_32();
        buz_set_freq_duty(AUD_PWM_BASE_FREQUENCY, 128);
    }
    uint32_t time = time_us_32();
    if (time > next_time)
    {
        next_time += 1000000 / AUDIO_DATA_HZ;
        buz_set_duty((uint8_t)(128 + (int8_t)audio_data[audio_data_offset++ % ARRAY_SIZE(audio_data)]));
    }
}
