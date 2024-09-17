/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "aud.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
#include "main.h"
#include <stdio.h>

#define ARRAY_SIZE(x) ((sizeof x) / (sizeof *x))

#include "../misc/audio_data.h"
static size_t audio_data_offset = 0;

#define SD1_SPI_READ_BIT 0x80 // Read/Write command bit on SD-1 bus

static struct
{
    uint slice_num;
    uint channel;
    uint gpio;
    uint16_t frequency;
    uint8_t duty;
    uint8_t wrap_shift;
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
    reg |= SD1_SPI_READ_BIT;
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
    buf[0] = reg & ~SD1_SPI_READ_BIT;
    buf[1] = data;
    aud_fm_select();
    spi_write_blocking(spi_default, buf, 2);
    aud_fm_deselect();
}

void aud_write_fm_register_multiple(uint8_t reg, uint8_t *data, uint16_t len)
{
    reg &= ~SD1_SPI_READ_BIT;
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
    gpio_set_function(AUD_SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(AUD_SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(AUD_SPI_TX_PIN, GPIO_FUNC_SPI);
    // Chip select is active-low, so we'll initialise it to a driven-high state
    gpio_init(AUD_SPI_CS_PIN);
    gpio_set_dir(AUD_SPI_CS_PIN, GPIO_OUT);
    gpio_put(AUD_SPI_CS_PIN, 1);
}

static void aud_fm_dump_registers(void)
{
    printf("\nSD-1 .0..1..2..3..4..5..6..7..8..9..A..B..C..D..E..F");
    for (uint8_t i = 0; i <= 80; ++i)
    {
        ;
        if (i % 0x10 == 0)
            printf("\n[%02X]", i);
        printf(" %02X", aud_read_fm_register(i));
    }
}

// 0: 5V / 1: 3.3V
#define OUTPUT_power 1

static void aud_fm_init825(void)
{
    // Select dual power configuration.
    aud_write_fm_register(0x1D, OUTPUT_power);
    // Set the AP0 to `0`. The VREF is powered.
    aud_write_fm_register(0x02, 0b00001110);
    // Wait until the clock becomes stable.
    sleep_ms(1);
    // Set the CLKE to `1`.
    aud_write_fm_register(0x00, 0x01);
    // Set the ALRST to `0`.
    aud_write_fm_register(0x01, 0x00);
    // Set the SFTRST to `A3`,
    aud_write_fm_register(0x1A, 0xA3);
    sleep_ms(1);
    // Set the SFTRST to `00`.
    aud_write_fm_register(0x1A, 0x00);
    // Wait 30 ms until the synthesizer block is initialized.
    sleep_ms(30);
    // Set the AP1 and the AP3 to `0`.
    aud_write_fm_register(0x02, 0b00000100);
    // Wait to prevent pop noise. (Use this time to set up the synthesizer etc.)
    sleep_us(10);
    // Set the AP2 to `0`.
    aud_write_fm_register(0x02, 0x00);
}

static void aud_fm_set_tone(void)
{
    unsigned char tone_data[35] = {
        0x81, // header
        // T_ADR 0
        0x01, 0x85,                               //
        0x00, 0x7F, 0xF4, 0xBB, 0x00, 0x10, 0x40, //
        0x00, 0xAF, 0xA0, 0x0E, 0x03, 0x10, 0x40, //
        0x00, 0x2F, 0xF3, 0x9B, 0x00, 0x20, 0x41, //
        0x00, 0xAF, 0xA0, 0x0E, 0x01, 0x10, 0x40, //
        0x80, 0x03, 0x81, 0x80,                   //
    };

    // Reset sequencer
    aud_write_fm_register(0x08, 0xF6);
    sleep_ms(1);
    aud_write_fm_register(0x08, 0x00);

    aud_write_fm_register_multiple(0x07, &tone_data[0], 35); // write to FIFO
}

static void aud_fm_set_ch(void)
{
    aud_write_fm_register(0x0B, 0x00); // voice num
    aud_write_fm_register(0x0F, 0x30); // keyon = 0
    aud_write_fm_register(0x10, 0x71); // chvol
    aud_write_fm_register(0x11, 0x00); // XVB
    aud_write_fm_register(0x12, 0x08); // FRAC
    aud_write_fm_register(0x13, 0x00); // FRAC
}

static void aud_fm_keyon(unsigned char fnumh, unsigned char fnuml)
{
    aud_write_fm_register(0x0B, 0x00);  // voice num
    aud_write_fm_register(0x0C, 0x54);  // vovol
    aud_write_fm_register(0x0D, fnumh); // fnum
    aud_write_fm_register(0x0E, fnuml); // fnum
    aud_write_fm_register(0x0F, 0x40);  // keyon = 1
}

static void aud_fm_keyoff(void)
{
    aud_write_fm_register(0x0F, 0x00); // keyon = 0
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
    pwm_channels[channel].wrap_shift = (uint8_t)wrap_shift;
    if (clock_div > 0)
    {
        pwm_set_clkdiv(pwm_channels[channel].slice_num, clock_div);
        pwm_set_wrap(pwm_channels[channel].slice_num, (uint16_t)(UINT8_MAX << wrap_shift));
    }
    pwm_set_chan_level(pwm_channels[channel].slice_num,
                       pwm_channels[channel].channel, (uint16_t)(duty << wrap_shift));
}

static void aud_pwm_set_channel_duty(size_t channel, uint8_t duty)
{
    pwm_channels[channel].duty = duty;
    if (pwm_channels[channel].frequency > 0)
    {
        pwm_set_chan_level(pwm_channels[channel].slice_num,
                           pwm_channels[channel].channel, (uint16_t)(duty << pwm_channels[channel].wrap_shift));
    }
}

static inline void aud_pwm_init(void)
{
    aud_pwm_init_channel(0, AUD_PWM_1_PIN);
    aud_pwm_init_channel(1, AUD_PWM_2_PIN);
}

static void aud_mix_reset(void)
{
    // Two byte reset. First byte register, second byte data
    uint8_t buf[] = {0xFE, 0b10000001};
    i2c_write_blocking(EXT_I2C, MIX_I2C_ADDRESS, buf, 2, false);
}

static inline void aud_mix_init(void)
{
    // Init the chip
    uint8_t buf[] = {0x01, 0b00000100};
    i2c_write_blocking(EXT_I2C, MIX_I2C_ADDRESS, buf, 2, false);
}

void aud_init(void)
{
    aud_fm_init();
    aud_pwm_init();
    aud_mix_init();

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
        aud_pwm_set_channel(i, (uint16_t)pwm_channels[i].frequency, pwm_channels[i].duty);
    }
}

static inline void aud_mix_reclock(void)
{
}

void aud_reclock(void)
{
    aud_fm_reclock();
    aud_pwm_reclock();
    aud_mix_reclock();
}

void aud_stop(void)
{
    aud_mix_reset();
}

void aud_task(void)
{
    static bool done = false;
    if (!done)
    {
        aud_fm_init825();

        // Configure playback
        // Set MASTER_VOL to +9dB
        aud_write_fm_register(0x19, 0b11110000);
        // Enable mute interpolation
        aud_write_fm_register(0x1B, 0b00111111);
        // Turn on interpolation
        aud_write_fm_register(0x14, 0x00);
        // Set speaker amplifier gain to 6.5dB (reset value)
        aud_write_fm_register(0x03, 0x01);

        // Reset sequencer
        aud_write_fm_register(0x08, 0b11110110);
        sleep_ms(21);
        aud_write_fm_register(0x08, 0x00);

        // Set sequencer volume
        aud_write_fm_register(0x09, 0b11111000);
        // Set sequence SIZE
        aud_write_fm_register(0x0A, 0x00);

        // Set sequencer time unit - MS_S
        aud_write_fm_register(0x17, 0x40);
        aud_write_fm_register(0x18, 0x00);

        aud_fm_set_tone();
        aud_fm_set_ch();

        // aud_fm_dump_registers();

        // aud_mix_init_volume
        uint8_t buf[] = {0x28,
                         // Set volume (gain / attenuation) on all IN to 0dB
                         0b10000000, 0b10000000, 0b10000000, 0b10000000, 0b10000000, 0b10000000,
                         // Activate all EXT on all channels and set volume (attenuation) to max (0db)
                         0b11111100, 0b11111100, 0b11111100, 0b00000000, 0b00000000, 0b00000000};
        i2c_write_blocking(EXT_I2C, MIX_I2C_ADDRESS, buf, 13, false);

        aud_pwm_set_channel(0, AUD_PWM_BASE_FREQUENCY, 128);

        done = true;
    }

    // heartbeat
    static bool was_on = false;
    bool on = (time_us_32() / 100000) % AUD_CLICK_DURATION_MS > 8;
    if (was_on != on)
    {
        // aud_pwm_set_channel(0, on ? AUD_CLICK_FREQUENCY : 0, AUD_CLICK_DUTY);
        was_on = on;
        if (on)
        {
            static uint16_t do_re_mi_pwm[] = {392, 349, 493, 523, 698, 587};
            static size_t i = 0;
            aud_pwm_set_channel(0, do_re_mi_pwm[i++ % 6], 128);

            // FM do-re-mi
            static uint8_t do_re_mi_fm[] = {
                0x14, 0x65, //
                0x1c, 0x11, //
                0x1c, 0x42, //
                0x1c, 0x5d, //
                0x24, 0x17, //
            };
            static size_t idx = 0;
            aud_fm_keyon(do_re_mi_fm[idx], do_re_mi_fm[idx + 1]);
            idx += 2;
            if (idx >= ARRAY_SIZE(do_re_mi_fm))
                idx = 0;
        }
    }
    // // play sampled music
    // static uint32_t next_time = 0;
    // uint32_t time = time_us_32();
    // if (time > next_time)
    // {
    //     next_time += clock_get_hz(clk_sys) / (((UINT8_MAX + 1) << pwm_channels[0].wrap_shift) * AUDIO_DATA_HZ);
    //     aud_pwm_set_channel_duty(0, (uint8_t)(128 + (int8_t)audio_data[audio_data_offset++ % ARRAY_SIZE(audio_data)]));
    //     // static uint8_t duty = 0;
    //     // aud_pwm_set_channel_duty(0, duty);
    //     // duty += 10;
    // }
}
