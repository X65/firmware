/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "aud.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
#include "main.h"

#include "aud.pio.h"

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

struct reg_default
{
    unsigned int reg;
    unsigned int def;
};

static const struct reg_default sgtl5000_reg_defaults[] = {
    {SGTL_CHIP_DIG_POWER, 0x0000},
    {SGTL_CHIP_I2S_CTRL, 0x0010},
    {SGTL_CHIP_SSS_CTRL, 0x0010},
    {SGTL_CHIP_ADCDAC_CTRL, 0x020c},
    {SGTL_CHIP_DAC_VOL, 0x3c3c},
    {SGTL_CHIP_PAD_STRENGTH, 0x015f},
    {SGTL_CHIP_ANA_ADC_CTRL, 0x0000},
    {SGTL_CHIP_ANA_HP_CTRL, 0x1818},
    {SGTL_CHIP_ANA_CTRL, 0x0111},
    {SGTL_CHIP_REF_CTRL, 0x0000},
    {SGTL_CHIP_MIC_CTRL, 0x0000},
    {SGTL_CHIP_LINE_OUT_CTRL, 0x0000},
    {SGTL_CHIP_LINE_OUT_VOL, 0x0404},
    {SGTL_CHIP_PLL_CTRL, 0x5000},
    {SGTL_CHIP_CLK_TOP_CTRL, 0x0000},
    {SGTL_CHIP_ANA_STATUS, 0x0000},
    {SGTL_CHIP_SHORT_CTRL, 0x0000},
    {SGTL_CHIP_ANA_TEST2, 0x0000},
};

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
        printf("Error reading I2S register %04x: %d\n", reg, ret);
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
    int ret = i2c_write_blocking_until(EXT_I2C, I2S_I2C_ADDRESS, buf, 4, false, make_timeout_time_ms(500));
    if (ret != 4)
    {
        printf("Error writing I2S register %04x with %04x: %d\n", reg, data, ret);
        return;
    }
}

static void sgtl5000_fill_defaults(void)
{
    int i, val, index;

    for (i = 0; i < ARRAY_SIZE(sgtl5000_reg_defaults); i++)
    {
        val = sgtl5000_reg_defaults[i].def;
        index = sgtl5000_reg_defaults[i].reg;
        aud_write_i2s_register(index, val);
    }
}

/* Delay for the VAG ramp up */
#define SGTL5000_VAG_POWERUP_DELAY   500 /* ms */
/* Delay for the VAG ramp down */
#define SGTL5000_VAG_POWERDOWN_DELAY 500 /* ms */

enum
{
    I2S_LRCLK_STRENGTH_DISABLE,
    I2S_LRCLK_STRENGTH_LOW,
    I2S_LRCLK_STRENGTH_MEDIUM,
    I2S_LRCLK_STRENGTH_HIGH,
};

enum
{
    I2S_SCLK_STRENGTH_DISABLE,
    I2S_SCLK_STRENGTH_LOW,
    I2S_SCLK_STRENGTH_MEDIUM,
    I2S_SCLK_STRENGTH_HIGH,
};

/*
 * SGTL5000_CHIP_LINREG_CTRL
 */
#define SGTL5000_VDDC_MAN_ASSN_MASK  0x0040
#define SGTL5000_VDDC_MAN_ASSN_SHIFT 6
#define SGTL5000_VDDC_MAN_ASSN_WIDTH 1
#define SGTL5000_VDDC_MAN_ASSN_VDDA  0x0
#define SGTL5000_VDDC_MAN_ASSN_VDDIO 0x1
#define SGTL5000_VDDC_ASSN_OVRD      0x0020
#define SGTL5000_LINREG_VDDD_MASK    0x000f
#define SGTL5000_LINREG_VDDD_SHIFT   0
#define SGTL5000_LINREG_VDDD_WIDTH   4

/*
 * SGTL5000_CHIP_ANA_POWER
 */
#define SGTL5000_ANA_POWER_DEFAULT     0x7060
#define SGTL5000_DAC_STEREO            0x4000
#define SGTL5000_LINREG_SIMPLE_POWERUP 0x2000
#define SGTL5000_STARTUP_POWERUP       0x1000
#define SGTL5000_VDDC_CHRGPMP_POWERUP  0x0800
#define SGTL5000_PLL_POWERUP           0x0400
#define SGTL5000_LINEREG_D_POWERUP     0x0200
#define SGTL5000_VCOAMP_POWERUP        0x0100
#define SGTL5000_VAG_POWERUP           0x0080
#define SGTL5000_ADC_STEREO            0x0040
#define SGTL5000_REFTOP_POWERUP        0x0020
#define SGTL5000_HP_POWERUP            0x0010
#define SGTL5000_DAC_POWERUP           0x0008
#define SGTL5000_CAPLESS_HP_POWERUP    0x0004
#define SGTL5000_ADC_POWERUP           0x0002
#define SGTL5000_LINE_OUT_POWERUP      0x0001

/*
 * SGTL5000_CHIP_REF_CTRL
 */
#define SGTL5000_ANA_GND_MASK    0x01f0
#define SGTL5000_ANA_GND_SHIFT   4
#define SGTL5000_ANA_GND_WIDTH   5
#define SGTL5000_ANA_GND_BASE    800 /* mv */
#define SGTL5000_ANA_GND_STP     25  /*mv */
#define SGTL5000_BIAS_CTRL_MASK  0x000e
#define SGTL5000_BIAS_CTRL_SHIFT 1
#define SGTL5000_BIAS_CTRL_WIDTH 3
#define SGTL5000_SMALL_POP       0x0001

/*
 * SGTL5000_CHIP_LINE_OUT_CTRL
 */
#define SGTL5000_LINE_OUT_CURRENT_MASK  0x0f00
#define SGTL5000_LINE_OUT_CURRENT_SHIFT 8
#define SGTL5000_LINE_OUT_CURRENT_WIDTH 4
#define SGTL5000_LINE_OUT_CURRENT_180u  0x0
#define SGTL5000_LINE_OUT_CURRENT_270u  0x1
#define SGTL5000_LINE_OUT_CURRENT_360u  0x3
#define SGTL5000_LINE_OUT_CURRENT_450u  0x7
#define SGTL5000_LINE_OUT_CURRENT_540u  0xf
#define SGTL5000_LINE_OUT_GND_MASK      0x003f
#define SGTL5000_LINE_OUT_GND_SHIFT     0
#define SGTL5000_LINE_OUT_GND_WIDTH     6
#define SGTL5000_LINE_OUT_GND_BASE      800 /* mv */
#define SGTL5000_LINE_OUT_GND_STP       25
#define SGTL5000_LINE_OUT_GND_MAX       0x23

/*
 * SGTL5000_CHIP_CLK_CTRL
 */
#define SGTL5000_CHIP_CLK_CTRL_DEFAULT 0x0008
#define SGTL5000_RATE_MODE_MASK        0x0030
#define SGTL5000_RATE_MODE_SHIFT       4
#define SGTL5000_RATE_MODE_WIDTH       2
#define SGTL5000_RATE_MODE_DIV_1       0
#define SGTL5000_RATE_MODE_DIV_2       1
#define SGTL5000_RATE_MODE_DIV_4       2
#define SGTL5000_RATE_MODE_DIV_6       3
#define SGTL5000_SYS_FS_MASK           0x000c
#define SGTL5000_SYS_FS_SHIFT          2
#define SGTL5000_SYS_FS_WIDTH          2
#define SGTL5000_SYS_FS_32k            0x0
#define SGTL5000_SYS_FS_44_1k          0x1
#define SGTL5000_SYS_FS_48k            0x2
#define SGTL5000_SYS_FS_96k            0x3
#define SGTL5000_MCLK_FREQ_MASK        0x0003
#define SGTL5000_MCLK_FREQ_SHIFT       0
#define SGTL5000_MCLK_FREQ_WIDTH       2
#define SGTL5000_MCLK_FREQ_256FS       0x0
#define SGTL5000_MCLK_FREQ_384FS       0x1
#define SGTL5000_MCLK_FREQ_512FS       0x2
#define SGTL5000_MCLK_FREQ_PLL         0x3

/*
 * SGTL5000_CHIP_ANA_CTRL
 */
#define SGTL5000_CHIP_ANA_CTRL_DEFAULT 0x0133
#define SGTL5000_LINE_OUT_MUTE         0x0100
#define SGTL5000_HP_SEL_MASK           0x0040
#define SGTL5000_HP_SEL_SHIFT          6
#define SGTL5000_HP_SEL_WIDTH          1
#define SGTL5000_HP_SEL_DAC            0x0
#define SGTL5000_HP_SEL_LINE_IN        0x1
#define SGTL5000_HP_ZCD_EN             0x0020
#define SGTL5000_HP_MUTE               0x0010
#define SGTL5000_ADC_SEL_MASK          0x0004
#define SGTL5000_ADC_SEL_SHIFT         2
#define SGTL5000_ADC_SEL_WIDTH         1
#define SGTL5000_ADC_SEL_MIC           0x0
#define SGTL5000_ADC_SEL_LINE_IN       0x1
#define SGTL5000_ADC_ZCD_EN            0x0002
#define SGTL5000_ADC_MUTE              0x0001

/*
 * SGTL5000_CHIP_DIG_POWER
 */
#define SGTL5000_DIG_POWER_DEFAULT 0x0000
#define SGTL5000_ADC_EN            0x0040
#define SGTL5000_DAC_EN            0x0020
#define SGTL5000_DAP_POWERUP       0x0010
#define SGTL5000_I2S_OUT_POWERUP   0x0002
#define SGTL5000_I2S_IN_POWERUP    0x0001

/*
 * SGTL5000_CHIP_ADCDAC_CTRL
 */
#define SGTL5000_VOL_BUSY_DAC_RIGHT 0x2000
#define SGTL5000_VOL_BUSY_DAC_LEFT  0x1000
#define SGTL5000_DAC_VOL_RAMP_EN    0x0200
#define SGTL5000_DAC_VOL_RAMP_EXPO  0x0100
#define SGTL5000_DAC_MUTE_RIGHT     0x0008
#define SGTL5000_DAC_MUTE_LEFT      0x0004
#define SGTL5000_ADC_HPF_FREEZE     0x0002
#define SGTL5000_ADC_HPF_BYPASS     0x0001

/*
 * SGTL5000_CHIP_PAD_STRENGTH
 */
#define SGTL5000_PAD_I2S_LRCLK_MASK  0x0300
#define SGTL5000_PAD_I2S_LRCLK_SHIFT 8
#define SGTL5000_PAD_I2S_LRCLK_WIDTH 2
#define SGTL5000_PAD_I2S_SCLK_MASK   0x00c0
#define SGTL5000_PAD_I2S_SCLK_SHIFT  6
#define SGTL5000_PAD_I2S_SCLK_WIDTH  2
#define SGTL5000_PAD_I2S_DOUT_MASK   0x0030
#define SGTL5000_PAD_I2S_DOUT_SHIFT  4
#define SGTL5000_PAD_I2S_DOUT_WIDTH  2
#define SGTL5000_PAD_I2C_SDA_MASK    0x000c
#define SGTL5000_PAD_I2C_SDA_SHIFT   2
#define SGTL5000_PAD_I2C_SDA_WIDTH   2
#define SGTL5000_PAD_I2C_SCL_MASK    0x0003
#define SGTL5000_PAD_I2C_SCL_SHIFT   0
#define SGTL5000_PAD_I2C_SCL_WIDTH   2

/*
 * SGTL5000_CHIP_I2S_CTRL
 */
#define SGTL5000_I2S_SCLKFREQ_MASK  0x0100
#define SGTL5000_I2S_SCLKFREQ_SHIFT 8
#define SGTL5000_I2S_SCLKFREQ_WIDTH 1
#define SGTL5000_I2S_SCLKFREQ_64FS  0x0
#define SGTL5000_I2S_SCLKFREQ_32FS  0x1 /* Not for RJ mode */
#define SGTL5000_I2S_MASTER         0x0080
#define SGTL5000_I2S_SCLK_INV       0x0040
#define SGTL5000_I2S_DLEN_MASK      0x0030
#define SGTL5000_I2S_DLEN_SHIFT     4
#define SGTL5000_I2S_DLEN_WIDTH     2
#define SGTL5000_I2S_DLEN_32        0x0
#define SGTL5000_I2S_DLEN_24        0x1
#define SGTL5000_I2S_DLEN_20        0x2
#define SGTL5000_I2S_DLEN_16        0x3
#define SGTL5000_I2S_MODE_MASK      0x000c
#define SGTL5000_I2S_MODE_SHIFT     2
#define SGTL5000_I2S_MODE_WIDTH     2
#define SGTL5000_I2S_MODE_I2S_LJ    0x0
#define SGTL5000_I2S_MODE_RJ        0x1
#define SGTL5000_I2S_MODE_PCM       0x2
#define SGTL5000_I2S_LRALIGN        0x0002
#define SGTL5000_I2S_LRPOL          0x0001 /* set for which mode */

static inline void aud_i2s_pio_init(void)
{
    uint i2s_pins[] = {
        AUD_I2S_LRCLK_PIN,
        AUD_I2S_SCLK_PIN,
        AUD_I2S_DIN_PIN,
        AUD_I2S_DOUT_PIN,
    };

    // Adjustments for GPIO performance. Important!
    for (int i = 0; i < 4; ++i)
    {
        uint pin = i2s_pins[i];
        gpio_set_pulls(pin, true, true);
        gpio_set_input_hysteresis_enabled(pin, false);
        hw_set_bits(&AUD_I2S_PIO->input_sync_bypass, 1u << pin);
    }

    pio_gpio_init(AUD_I2S_PIO, AUD_I2S_DOUT_PIN);
    pio_gpio_init(AUD_I2S_PIO, AUD_I2S_PIN_BASE);
    pio_gpio_init(AUD_I2S_PIO, AUD_I2S_PIN_BASE + 1);
    pio_gpio_init(AUD_I2S_PIO, AUD_I2S_PIN_BASE + 2);

    uint offset = pio_add_program(AUD_I2S_PIO, &aud_i2s_program);
    pio_sm_config sm_config = aud_i2s_program_get_default_config(offset);
    sm_config_set_out_pins(&sm_config, AUD_I2S_DOUT_PIN, 1);
    sm_config_set_in_pins(&sm_config, AUD_I2S_PIN_BASE);
    sm_config_set_jmp_pin(&sm_config, AUD_I2S_PIN_BASE + 2);
    sm_config_set_out_shift(&sm_config, false, false, 0);
    sm_config_set_in_shift(&sm_config, false, false, 0);
    pio_sm_init(AUD_I2S_PIO, AUD_I2S_SM, offset + aud_i2s_offset_entry_point, &sm_config);

    // Setup output pins
    uint32_t pin_mask = (1u << AUD_I2S_DOUT_PIN);
    pio_sm_set_pins_with_mask(AUD_I2S_PIO, AUD_I2S_SM, 0, pin_mask);
    pio_sm_set_pindirs_with_mask(AUD_I2S_PIO, AUD_I2S_SM, pin_mask, pin_mask);

    // Setup input pins
    pin_mask = (7u << AUD_I2S_PIN_BASE); // Three input pins
    pio_sm_set_pindirs_with_mask(AUD_I2S_PIO, AUD_I2S_SM, 0, pin_mask);

    pio_sm_set_enabled(AUD_I2S_PIO, AUD_I2S_SM, true);
}

static inline void aud_i2s_reclock(void)
{
    // Clock synchronously with the system clock
}

/** Init the codec chip over I2C */
static inline void aud_i2s_reg_init(void)
{
    // https://github.com/torvalds/linux/blob/master/sound/soc/codecs/sgtl5000.h

    /* reconfigure the clocks in case we're using the PLL */
    aud_write_i2s_register(
        SGTL_CHIP_CLK_CTRL,
        SGTL5000_CHIP_CLK_CTRL_DEFAULT);

    /* Mute everything to avoid pop from the following power-up */
    aud_write_i2s_register(SGTL_CHIP_ANA_CTRL,
                           SGTL5000_CHIP_ANA_CTRL_DEFAULT);

    /*
     * If VAG is powered-on (e.g. from previous boot), it would be disabled
     * by the write to ANA_POWER in later steps of the probe code. This
     * may create a loud pop even with all outputs muted. The proper way
     * to circumvent this is disabling the bit first and waiting the proper
     * cool-down time.
     */
    uint16_t ana_pwr = aud_read_i2s_register(SGTL_CHIP_ANA_POWER);

    if (ana_pwr & SGTL5000_VAG_POWERUP)
    {
        ana_pwr &= SGTL5000_VAG_POWERUP;
        aud_write_i2s_register(SGTL_CHIP_ANA_POWER, ana_pwr);

        sleep_ms(SGTL5000_VAG_POWERDOWN_DELAY);
    }

    /* Follow section 2.2.1.1 of AN3663 */
    ana_pwr = SGTL5000_ANA_POWER_DEFAULT;
    /* using external LDO for VDDD
     * Clear startup powerup and simple powerup
     * bits to save power
     */
    ana_pwr &= ~(SGTL5000_STARTUP_POWERUP
                 | SGTL5000_LINREG_SIMPLE_POWERUP);
    aud_write_i2s_register(SGTL_CHIP_ANA_POWER, ana_pwr);

    /* Ensure sgtl5000 will start with sane register values */
    sgtl5000_fill_defaults();

    /* power up sgtl5000 */
    // ret = sgtl5000_set_power_regs(component);

    /* reset value */
    ana_pwr = aud_read_i2s_register(SGTL_CHIP_ANA_POWER);
    ana_pwr |= SGTL5000_DAC_STEREO | SGTL5000_ADC_STEREO | SGTL5000_REFTOP_POWERUP;
    uint16_t lreg_ctrl = aud_read_i2s_register(SGTL_CHIP_LINREG_CTRL);

    ana_pwr &= ~SGTL5000_VDDC_CHRGPMP_POWERUP;

    /*
     * if vddio == vdda the source of charge pump should be
     * assigned manually to VDDIO
     */
    lreg_ctrl |= SGTL5000_VDDC_ASSN_OVRD;
    lreg_ctrl |= SGTL5000_VDDC_MAN_ASSN_VDDIO << SGTL5000_VDDC_MAN_ASSN_SHIFT;

    aud_write_i2s_register(SGTL_CHIP_LINREG_CTRL, lreg_ctrl);

    aud_write_i2s_register(SGTL_CHIP_ANA_POWER, ana_pwr);

    /* become I2S master*/
    //     // Configure PLL parameters
    //     aud_write_i2s_register(SGTL_CHIP_CLK_TOP_CTRL, 0);
    // #define PLL_OUTPUT_FREQ_KHZ (196608 / 2)
    // #define INT_DIVISOR         ((int)(PLL_OUTPUT_FREQ_KHZ / AUD_CLOCK_FREQUENCY_KHZ))
    // #define FRAC_DIVISOR        (((PLL_OUTPUT_FREQ_KHZ / AUD_CLOCK_FREQUENCY_KHZ) - INT_DIVISOR) * 2048)
    //     aud_write_i2s_register(SGTL_CHIP_PLL_CTRL, (INT_DIVISOR << 11) | (FRAC_DIVISOR & 0x7FF));
    // Select SYS_FS and MCLK_FREQ
    // SCLK=32*Fs, 16bit, I2S format
    aud_write_i2s_register(SGTL_CHIP_I2S_CTRL, 0x0130 | SGTL5000_I2S_MASTER);
    //     // Enable the PLL
    //     ana_pwr |= SGTL5000_PLL_POWERUP | SGTL5000_VCOAMP_POWERUP;
    //     aud_write_i2s_register(SGTL_CHIP_ANA_POWER, ana_pwr);
    //     // Route PLL to system clock
    //     aud_write_i2s_register(SGTL_CHIP_CLK_CTRL, SGTL5000_CHIP_CLK_CTRL_DEFAULT | SGTL5000_MCLK_FREQ_PLL);
    // SYS_FS is rate, Fs=48kHz, 256*Fs :: (256*48kHz(Fs) => 12.288Mhz(MCLK))
    aud_write_i2s_register(SGTL_CHIP_CLK_CTRL,
                           SGTL5000_CHIP_CLK_CTRL_DEFAULT
                               | (SGTL5000_RATE_MODE_DIV_6 << SGTL5000_RATE_MODE_SHIFT));

    /*
     * set ADC/DAC VAG to vdda / 2,
     * should stay in range (0.8v, 1.575v)
     */
    uint16_t ref_ctrl = aud_read_i2s_register(SGTL_CHIP_REF_CTRL);
    ref_ctrl &= ~SGTL5000_ANA_GND_MASK;
    ref_ctrl |= 0x1F << SGTL5000_ANA_GND_SHIFT; // 1.575 V
    aud_write_i2s_register(SGTL_CHIP_REF_CTRL, ref_ctrl);

    /* set line out VAG to vddio / 2, in range (0.8v, 1.675v) */
    uint16_t line_out_ctrl = aud_read_i2s_register(SGTL_CHIP_LINE_OUT_CTRL);
    line_out_ctrl &= ~(SGTL5000_LINE_OUT_CURRENT_MASK | SGTL5000_LINE_OUT_GND_MASK);
    line_out_ctrl |= 0x1F << SGTL5000_LINE_OUT_GND_SHIFT | SGTL5000_LINE_OUT_CURRENT_360u << SGTL5000_LINE_OUT_CURRENT_SHIFT;
    aud_write_i2s_register(SGTL_CHIP_LINE_OUT_CTRL, line_out_ctrl);

    /* enable small pop, introduce 400ms delay in turning off */
    ref_ctrl &= ~SGTL5000_SMALL_POP;
    ref_ctrl |= SGTL5000_SMALL_POP;
    aud_write_i2s_register(SGTL_CHIP_REF_CTRL, ref_ctrl);

    /* disable short cut detector */
    aud_write_i2s_register(SGTL_CHIP_SHORT_CTRL, 0);

    /* enable dac volume ramp by default */
    aud_write_i2s_register(SGTL_CHIP_ADCDAC_CTRL,
                           SGTL5000_DAC_VOL_RAMP_EN | SGTL5000_DAC_MUTE_RIGHT | SGTL5000_DAC_MUTE_LEFT);

    aud_write_i2s_register(SGTL_CHIP_PAD_STRENGTH, (I2S_LRCLK_STRENGTH_LOW) << SGTL5000_PAD_I2S_LRCLK_SHIFT | (I2S_SCLK_STRENGTH_LOW) << SGTL5000_PAD_I2S_SCLK_SHIFT | 0x1f);

    // disable zero cross detectors
    uint16_t ana_ctrl = aud_read_i2s_register(SGTL_CHIP_ANA_CTRL);
    ana_ctrl &= ~(SGTL5000_HP_ZCD_EN | SGTL5000_ADC_ZCD_EN);
    aud_write_i2s_register(SGTL_CHIP_ANA_CTRL, ana_ctrl);

    // VAG power on
    ana_pwr &= ~SGTL5000_VAG_POWERUP;
    ana_pwr |= SGTL5000_VAG_POWERUP;
    aud_write_i2s_register(SGTL_CHIP_ANA_POWER, ana_pwr);
    /* When VAG powering on to get local loop from Line-In, the sleep
     * is required to avoid loud pop.
     */
    sleep_ms(SGTL5000_VAG_POWERUP_DELAY);

    /* Unmute DAC after start */
    uint16_t adcdac_ctrl = aud_read_i2s_register(SGTL_CHIP_ADCDAC_CTRL);
    adcdac_ctrl &= ~(SGTL5000_DAC_MUTE_LEFT | SGTL5000_DAC_MUTE_RIGHT);
    aud_write_i2s_register(SGTL_CHIP_ADCDAC_CTRL, adcdac_ctrl);

    // Power up LINEOUT, ADC, DAC
    ana_pwr |= SGTL5000_LINE_OUT_POWERUP | SGTL5000_ADC_POWERUP | SGTL5000_DAC_POWERUP;
    aud_write_i2s_register(SGTL_CHIP_ANA_POWER, ana_pwr);

    // Power up desired digital blocks
    // I2S_IN (bit 0), I2S_OUT (bit 1), DAC (bit 5),
    // ADC (bit 6) are powered on
    aud_write_i2s_register(SGTL_CHIP_DIG_POWER,
                           SGTL5000_I2S_IN_POWERUP | SGTL5000_I2S_OUT_POWERUP | SGTL5000_DAC_EN | SGTL5000_ADC_EN);

    // Set the LINEOUT volume level based on voltage reference (VAG)
    // values using this formula
    // Value = (int)(40*log(VAG_VAL/LO_VAGCNTRL) + 15)
    // Assuming VAG_VAL and LO_VAGCNTRL is set to 0.9 V and
    // 1.65 V respectively, the // left LO vol (bits 12:8) and right LO
    // volume (bits 4:0) value should be set // to 5
    aud_write_i2s_register(SGTL_CHIP_LINE_OUT_VOL, 0x1D1D); // default approx 1.3 volts peak-to-peak
    // aud_write_i2s_register(SGTL_CHIP_LINE_OUT_VOL, 0x0F0F); // Table 32

    // default signal routing is ok?
    aud_write_i2s_register(SGTL_CHIP_SSS_CTRL, 0x0010); // ADC->I2S, I2S->DAC

    // Configure DAC left and right digital volume.
    // Example shows volume of 0dB
    aud_write_i2s_register(SGTL_CHIP_DAC_VOL, 0x3C3C); // digital gain, 0dB

    // Unmute ADC, LINEOUT
    ana_ctrl &= ~(SGTL5000_ADC_MUTE | SGTL5000_LINE_OUT_MUTE);
    // Select ADC input LINEIN,
    ana_ctrl |= (SGTL5000_ADC_SEL_LINE_IN << SGTL5000_ADC_SEL_SHIFT);
    aud_write_i2s_register(SGTL_CHIP_ANA_CTRL, 0x0014);
}

void aud_i2s_dump_registers(void)
{
    printf("\nDAC registers dump\n");
    printf("CHIP_ID\t%04x\n", aud_read_i2s_register(0x0000));
    printf("CHIP_DIG_POWER\t%04x\n", aud_read_i2s_register(0x0002));
    printf("CHIP_CLK_CTRL\t%04x\n", aud_read_i2s_register(0x0004));
    printf("CHIP_I2S_CTRL\t%04x\n", aud_read_i2s_register(0x0006));
    printf("CHIP_SSS_CTRL\t%04x\n", aud_read_i2s_register(0x000A));
    printf("CHIP_ADCDAC_CTRL\t%04x\n", aud_read_i2s_register(0x000E));
    printf("CHIP_DAC_VOL\t%04x\n", aud_read_i2s_register(0x0010));
    printf("CHIP_PAD_STRENGTH\t%04x\n", aud_read_i2s_register(0x0014));
    printf("CHIP_ANA_ADC_CTRL\t%04x\n", aud_read_i2s_register(0x0020));
    printf("CHIP_ANA_HP_CTRL\t%04x\n", aud_read_i2s_register(0x0022));
    printf("CHIP_ANA_CTRL\t%04x\n", aud_read_i2s_register(0x0024));
    printf("CHIP_LINREG_CTRL\t%04x\n", aud_read_i2s_register(0x0026));
    printf("CHIP_REF_CTRL\t%04x\n", aud_read_i2s_register(0x0028));
    printf("CHIP_MIC_CTRL\t%04x\n", aud_read_i2s_register(0x002A));
    printf("CHIP_LINE_OUT_CTRL\t%04x\n", aud_read_i2s_register(0x002C));
    printf("CHIP_LINE_OUT_VOL\t%04x\n", aud_read_i2s_register(0x002E));
    printf("CHIP_ANA_POWER\t%04x\n", aud_read_i2s_register(0x0030));
    printf("CHIP_PLL_CTRL\t%04x\n", aud_read_i2s_register(0x0032));
    printf("CHIP_CLK_TOP_CTRL\t%04x\n", aud_read_i2s_register(0x0034));
    printf("CHIP_ANA_STATUS\t%04x\n", aud_read_i2s_register(0x0036));
    printf("CHIP_ANA_TEST1\t%04x\n", aud_read_i2s_register(0x0038));
    printf("CHIP_ANA_TEST2\t%04x\n", aud_read_i2s_register(0x003A));
    printf("CHIP_SHORT_CTRL\t%04x\n", aud_read_i2s_register(0x003C));
}

static void dma_i2s_int_handler(void)
{
    // dma_hw->ints0 = 1u << i2s.dma_ch_in_data;  // clear the IRQ
}

static inline void aud_i2s_init(void)
{
    // the inits
    aud_i2s_reg_init();
    aud_i2s_pio_init();

    // Set clocks
    aud_i2s_reclock();
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

    // play sampled sound
    static int16_t sample = 0;
    while (!pio_sm_is_tx_fifo_full(AUD_I2S_PIO, AUD_I2S_SM))
    {
        // pio_sm_put_blocking(AUD_I2S_PIO, AUD_I2S_SM, get_rand_32());
        // uint32_t s = sample >= 0 ? sample : 0;
        pio_sm_put_blocking(AUD_I2S_PIO, AUD_I2S_SM, (sample & 0xFFFF) << 16 | (sample & 0xFFFF));
        sample += 0x480;
    }
    // // play sampled music
    // static size_t audio_data_offset = 0;
    // while (!pio_sm_is_tx_fifo_full(AUD_I2S_PIO, AUD_I2S_SM))
    // {
    //     // pio_sm_put_blocking(AUD_I2S_PIO, AUD_I2S_SM, get_rand_32());
    //     int16_t sample = (int16_t)(audio_data[audio_data_offset++ % ARRAY_SIZE(audio_data)] << 8);
    //     pio_sm_put_blocking(AUD_I2S_PIO, AUD_I2S_SM, (sample & 0xFFFF) << 16 | (sample & 0xFFFF));
    //     sample += 1;
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
            // aud_i2s_dump_registers();
        }
        else
        {
            printf("Unknown (0x%04X)\n", id);
        }
    }
}
