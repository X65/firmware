/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "aud.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
#include "main.h"
#include <stdio.h>

#define ARRAY_SIZE(x) ((sizeof x) / (sizeof *x))

#define SD1_SPI_READ_BIT 0x80 // Read/Write command bit on SD-1 bus

// SGTL5000 Register Addresses
#define SGTL_CHIP_ID            0x0000
#define SGTL_CHIP_DIG_POWER     0x0002
#define SGTL_CHIP_CLK_CTRL      0x0004
#define SGTL_CHIP_I2S_CTRL      0x0006
#define SGTL_CHIP_SSS_CTRL      0x000A
#define SGTL_CHIP_ADCDAC_CTRL   0x000E
#define SGTL_CHIP_DAC_VOL       0x0010
#define SGTL_CHIP_PAD_STRENGTH  0x0014
#define SGTL_CHIP_ANA_ADC_CTRL  0x0020
#define SGTL_CHIP_ANA_HP_CTRL   0x0022
#define SGTL_CHIP_ANA_CTRL      0x0024
#define SGTL_CHIP_LINREG_CTRL   0x0026
#define SGTL_CHIP_REF_CTRL      0x0028
#define SGTL_CHIP_MIC_CTRL      0x002A
#define SGTL_CHIP_LINE_OUT_CTRL 0x002C
#define SGTL_CHIP_LINE_OUT_VOL  0x002E
#define SGTL_CHIP_ANA_POWER     0x0030
#define SGTL_CHIP_PLL_CTRL      0x0032
#define SGTL_CHIP_CLK_TOP_CTRL  0x0034
#define SGTL_CHIP_ANA_STATUS    0x0036
#define SGTL_CHIP_ANA_TEST1     0x0038
#define SGTL_CHIP_ANA_TEST2     0x003A
#define SGTL_CHIP_SHORT_CTRL    0x003C

// --- PWM - to be removed ------------------
// stubs:
#define AUD_CHANGE_DURATION_MS 10
void aud_pwm_set_channel(size_t channel, uint16_t freq, uint8_t duty) { };
void aud_pwm_set_channel_duty(size_t channel, uint8_t duty) { };

// --- FM synth ------------------
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

static inline void aud_fm_reclock(void)
{
    clock_configure(clk_gpout0,
                    CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC, 0,
                    clock_get_hz(clk_sys),
                    AUD_CLOCK_FREQUENCY_KHZ * 1000);

    spi_set_baudrate(AUD_SPI, AUD_BAUDRATE_HZ);
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

// --- I2S audio codec ---------------------
int aud_read_i2s_register(uint16_t reg)
{
    uint8_t buf[2];
    buf[0] = reg >> 8;
    buf[1] = reg & 0xFF;
    // write address
    i2c_write_blocking(EXT_I2C, I2S_I2C_ADDRESS, buf, 2, true);
    // read register value
    int ret = i2c_read_blocking_until(EXT_I2C, I2S_I2C_ADDRESS, buf, 2, false, make_timeout_time_ms(500));
    if (ret < 0)
    {
        // printf("Error reading I2S register %d: %d\n", reg, ret);
        return ret;
    }
    return (buf[0] << 8) | buf[1];
}

void aud_write_i2s_register(uint16_t reg, uint16_t data)
{
    uint8_t buf[4];
    buf[0] = reg >> 8;
    buf[1] = reg & 0xFF;
    buf[2] = data >> 8;
    buf[3] = data & 0xFF;
    i2c_write_blocking(EXT_I2C, I2S_I2C_ADDRESS, buf, 4, true);
}

/** Init the codec chip over I2C */
static inline void aud_i2s_init(void)
{
    /* Chip Powerup and Supply Configurations */

    //--------------- Power Supply Configuration----------------
    // Turn off startup power supplies to save power (Clear bit 12 and 13)
    // PLL Power Up (Set bit 10)
    aud_write_i2s_register(SGTL_CHIP_ANA_POWER, 0x4060);
    //---- Reference Voltage and Bias Current Configuration----
    // NOTE: The value written in the next 2 Write calls is dependent
    // on the VDDA voltage value.
    // Set ground, ADC, DAC reference voltage (bits 8:4). The value should
    // be set to VDDA/2. (1.55 V)
    // The bias current should be set to 50% of the nominal value (bits 3:1)
    aud_write_i2s_register(SGTL_CHIP_REF_CTRL, 0x01EE);
    // Set LINEOUT reference voltage to VDDIO/2 (1.55 V) (bits 5:0)
    // and bias current (bits 11:8) to the recommended value of 0.36 mA
    // for 10 kOhm load with 1.0 nF capacitance (0x3)
    aud_write_i2s_register(SGTL_CHIP_LINE_OUT_CTRL, 0x031E);
    //------------Power up Inputs/Outputs/Digital Blocks---------
    // Power up LINEOUT, ADC, DAC
    aud_write_i2s_register(SGTL_CHIP_ANA_POWER, 0x4060 | 0x2B);
    // Power up desired digital blocks
    // I2S_IN (bit 0), I2S_OUT (bit 1), DAC (bit 5),
    // ADC (bit 6) are powered on
    aud_write_i2s_register(SGTL_CHIP_DIG_POWER, 0x0063);
    //----------------Set LINEOUT Volume Level-------------------
    // Set the LINEOUT volume level based on voltage reference (VAG)
    // values using this formula
    // Value = (int)(40*log(VAG_VAL/LO_VAGCNTRL) + 15)
    // Assuming VAG_VAL and LO_VAGCNTRL is set to 1.55 V and 1.55 V respectively, the
    // left LO vol (bits 12:8) and right LO volume (bits 4:0) value should be set
    // to 0xF
    aud_write_i2s_register(SGTL_CHIP_LINE_OUT_VOL, 0x0F0F);

    /* PLL Configuration */

    // PLL output frequency is different based on the sample clock rate used.
    // if (Sys_Fs_Rate == 44.1 kHz)
    //  PLL_Output_Freq = 180.6336 MHz
    // else
    //  PLL_Output_Freq = 196.608 MHz
    // Set the PLL dividers
    int Int_Divisor = 196608 / AUD_CLOCK_FREQUENCY_KHZ;
    int Frac_Divisor = ((196608 / AUD_CLOCK_FREQUENCY_KHZ) - Int_Divisor) * 2048;
    aud_write_i2s_register(SGTL_CHIP_PLL_CTRL, ((Int_Divisor << 11) & 0xF800) | (Frac_Divisor & 0x7FF));

    // Power up PLL
    aud_write_i2s_register(SGTL_CHIP_ANA_POWER, 0x4060 | 0x2B | 0x0500);

    /* System MCLK and Sample Clock */

    // Configure SYS_FS clock to 48 kHz
    // Configure MCLK_FREQ to use the PLL output
    aud_write_i2s_register(SGTL_CHIP_CLK_CTRL, (0x2 << 2) | 0x3);
    // Configure the I2S clocks in master mode (bit 7)
    // NOTE: I2S LRCLK is same as the system sample clock
    // I2S data length is 16 bit
    aud_write_i2s_register(SGTL_CHIP_I2S_CTRL, 0x00B0);

    /* Input/Output Routing */

    // I2S_IN -> DAC -> LINEOUT
    // LINEIN -> ADC -> I2S_OUT
    aud_write_i2s_register(SGTL_CHIP_SSS_CTRL, 0x0010);
    // Route and unmute analog path
    aud_write_i2s_register(SGTL_CHIP_ANA_CTRL, 0x0014);
}

static inline void aud_i2s_reclock(void)
{
}

// --- I2C audio mixer ---------------------
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

static inline void aud_mix_reclock(void)
{
}

// --- Audio main ---------------------
void aud_init(void)
{
    aud_fm_init();
    aud_i2s_init();
    aud_mix_init();

    // Set clocks
    aud_reclock();
}

void aud_reclock(void)
{
    aud_fm_reclock();
    aud_i2s_reclock();
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

        done = true;
    }

    // heartbeat
    static bool was_on = false;
    bool on = (time_us_32() / 100000) % AUD_CHANGE_DURATION_MS > 8;
    if (was_on != on)
    {
        was_on = on;
        if (on)
        {
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

void aud_print_status(void)
{
    printf("DAC: ");
    int id = aud_read_i2s_register(SGTL_CHIP_ID);
    if (id < 0)
    {
        printf("Not present\n");
    }
    else
    {
        if ((id & 0xFF00) == 0xA000)
        {
            printf("SGTL5000 rev.%d\n", id & 0xFF);
        }
        else
        {
            printf("Unknown (0x%04X)\n", id);
        }
    }
}
