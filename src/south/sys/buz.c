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
#include <math.h>
#include <stdio.h>

static uint slice_num;
static uint channel;

static double frequency;
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

void buz_set_duty(uint8_t duty)
{
    duty_cycle = duty;
    pwm_set_chan_level(slice_num,
                       channel, (uint16_t)(duty_cycle << wrap_shift));
}

void buz_set_freq_duty(double f_hz, uint8_t duty)
{
    double clock_div = 0;
    int wrap_sh = 0;
    if (f_hz < 20.0f)
    {
        duty = 0;
    }
    else
    {
        double clock_div_base = (double)(clock_get_hz(clk_sys)) / f_hz;
        do
        {
            clock_div = clock_div_base / (float)((UINT8_MAX + 1) << wrap_sh) / 2;
        } while (clock_div > 256.f && wrap_sh++ < 9);

        if (wrap_sh > 8)
        {
            printf("? cannot handle channel %d frequency: %f\n", channel, f_hz);
        }
    }

    frequency = f_hz;
    duty_cycle = duty;
    wrap_shift = (uint8_t)wrap_sh;
    if (clock_div > 0)
    {
        pwm_set_clkdiv(slice_num, (float)clock_div);
        pwm_set_wrap(slice_num, (uint16_t)(UINT8_MAX << wrap_shift));
    }

    buz_set_duty(duty);
}

/* Buzzer frequency encoding:
 *
 * The MMIO FREQ register is a 16-bit *logarithmic pitch* value, not a linear Hz/divider.
 * We map the full 0..65535 range onto ~20 Hz..20 kHz by making FREQ linear in log2(f):
 *
 *   f(FREQ) = 20 Hz * 2^(10 * FREQ / 65535)
 *
 * This gives a clock-independent, future-proof ABI (no dependence on RP2350 f_sys),
 * and near-uniform perceptual resolution across the audible range (pitch is logarithmic).
 * It also makes pitch slides trivial: add/subtract a constant to FREQ for a smooth slide.
 */

#define F_min   (20)
#define F_max   (20 * KHZ)
// OCTAVES = log2(F_max / F_min) ≈ log2(1000) ≈ 9.965784285
#define OCTAVES (9.965784285)

void buz_set_freq16(uint16_t freq)
{
    double f = 20 * pow(2, OCTAVES * (double)freq / 65535);
    buz_set_freq_duty(f, duty_cycle);
}

// #define ARRAY_SIZE(x) ((sizeof x) / (sizeof *x))

// #include "../../../.././examples/src/cgia/data/audio_data.h"
// static size_t audio_data_offset = 0;
// #define AUD_PWM_BASE_FREQUENCY 40000

void buz_task(void)
{
    // heartbeat
    static bool was_on = false;
    bool on = (time_us_32() / 100000) % BUZ_CLICK_DURATION_MS > 8;
    if (was_on != on)
    {
        // buz_set_freq_duty(on ? BUZ_CLICK_FREQUENCY : 0, BUZ_CLICK_DUTY);
        was_on = on;

        if (on)
        {
            static uint16_t do_re_mi_pwm[] = {24393, 25489, 26585, 27133, 28229, 29325, 30421, 30969};
            static size_t i = 0;
            buz_set_duty(128);
            buz_set_freq16(do_re_mi_pwm[i++ % 8]);
        }
    }

    // // play sampled music
    // static uint32_t next_time = 0;
    // if (next_time == 0)
    // {
    //     next_time = time_us_32();
    //     buz_set_freq_duty(AUD_PWM_BASE_FREQUENCY, 128);
    // }
    // uint32_t time = time_us_32();
    // if (time > next_time)
    // {
    //     next_time += 1000000 / AUDIO_DATA_HZ;
    //     buz_set_duty((uint8_t)(128 + (int8_t)audio_data[audio_data_offset++ % ARRAY_SIZE(audio_data)]));
    // }
}
