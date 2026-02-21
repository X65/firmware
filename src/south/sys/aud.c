/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "./aud.h"
#include "./out.h"
#include "aud.pio.h"
#include "hw.h"

#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/spi.h>
#include <pico/types.h>
#include <stdio.h>

#define SPI_READ_BIT 0x8000

#define USE_MIRROR_REGS (1)
#if USE_MIRROR_REGS
static uint8_t reg_bank = 0;
static uint8_t reg_mirror[10 * 64] = {0};
#endif

uint8_t aud_read_register(uint8_t reg)
{
#if USE_MIRROR_REGS
    if ((reg & 0x3F) == 0x3F)
    {
        return reg_bank;
    }
    else
    {
        return reg_mirror[reg_bank * 64 + (reg & 0x3F)];
    }
#endif
    uint16_t packet = (uint16_t)(SPI_READ_BIT | ((uint16_t)(reg & 0x3F) << 8));
    int retries = 10;
    uint16_t response = 0;
    while (retries-- > 0)
    {
        spi_write16_read16_blocking(AUD_SPI, &packet, &response, 1);
        if ((response & 0xFF00) == (packet & 0xFF00))
            return (uint8_t)(response);
    }
    return 0xFF;
}

void aud_write_register(uint8_t reg, uint8_t data)
{
#if USE_MIRROR_REGS
    if ((reg & 0x3F) == 0x3F)
    {
        // bank select register - update bank index
        reg_bank = data & 0x0F;
    }
    else
    {
        reg_mirror[reg_bank * 64 + (reg & 0x3F)] = data;
    }
#endif
    uint16_t packet = (uint16_t)(((uint16_t)(reg & 0x3F) << 8) | data);
    spi_write16_blocking(AUD_SPI, &packet, 1);
}

static void aud_i2s_rx_irq_handler()
{
    // PIO packs stereo into one 32-bit word: (left << 16) | right.
    while (!pio_sm_is_rx_fifo_empty(AUD_I2S_PIO, AUD_I2S_SM))
    {
        uint32_t word = pio_sm_get(AUD_I2S_PIO, AUD_I2S_SM);
        out_audio_submit((int16_t)(word >> 16), (int16_t)(word & 0xFFFF));
    }
}

static inline void aud_i2s_pio_init(void)
{
    // pio_set_gpio_base(AUD_I2S_PIO, 16);

    uint i2s_pins[] = {
        AUD_I2S_DIN_PIN,
        AUD_I2S_SCLK_PIN,
        AUD_I2S_LRCLK_PIN,
    };

    // Adjustments for noise level. Important!
    for (int i = 0; i < 3; ++i)
    {
        uint pin = i2s_pins[i];
        pio_gpio_init(AUD_I2S_PIO, pin);
        gpio_set_pulls(pin, false, false);
        gpio_set_input_hysteresis_enabled(pin, false);
        gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_2MA);
        gpio_set_slew_rate(pin, GPIO_SLEW_RATE_SLOW);
    }

    pio_sm_claim(AUD_I2S_PIO, AUD_I2S_SM);
    uint offset = pio_add_program(AUD_I2S_PIO, &aud_i2s_program);
    pio_sm_config sm_config = aud_i2s_program_get_default_config(offset);
    sm_config_set_in_pins(&sm_config, AUD_I2S_PIN_BASE);
    sm_config_set_sideset_pins(&sm_config, AUD_I2S_SCLK_PIN);
    sm_config_set_in_shift(&sm_config, false, false, 32);
    sm_config_set_fifo_join(&sm_config, PIO_FIFO_JOIN_RX);
    // PIO runs at BCK * 2 = 48000 Hz * 16 bits * 2 channels * 2 = 3.072 MHz
    float pio_freq = 48000.0f * 16 * 2 * 2;
    uint32_t sys_hz = clock_get_hz(clk_sys);
    float div = (float)sys_hz / pio_freq;
    sm_config_set_clkdiv(&sm_config, div);
    pio_sm_init(AUD_I2S_PIO, AUD_I2S_SM, offset + aud_i2s_offset_entry_point, &sm_config);

    // Setup input pins
    pio_sm_set_consecutive_pindirs(AUD_I2S_PIO, AUD_I2S_SM, AUD_I2S_DIN_PIN, 1, false);
    pio_sm_set_consecutive_pindirs(AUD_I2S_PIO, AUD_I2S_SM, AUD_I2S_SCLK_PIN, 2, true);

    // Connect the interrupt handler (higher priority than DMA video IRQ
    // to prevent FIFO overflow and audio sample loss)
    const int num = pio_get_irq_num(AUD_I2S_PIO, 0);
    irq_set_exclusive_handler(num, aud_i2s_rx_irq_handler);
    irq_set_priority(num, PICO_HIGHEST_IRQ_PRIORITY);
    pio_interrupt_source_t src = pio_get_rx_fifo_not_empty_interrupt_source(AUD_I2S_SM);
    pio_set_irqn_source_enabled(AUD_I2S_PIO, 0, src, true);
    irq_set_enabled(num, true);

    pio_sm_set_enabled(AUD_I2S_PIO, AUD_I2S_SM, true);
}

void aud_init(void)
{
#ifdef AUD_CLOCK_PIN
    // Generate clock for SGU-1 using CLK_GPOUT0
    // This is used on proto-boards only and is not needed on DEV-board.
    gpio_set_function(AUD_CLOCK_PIN, GPIO_FUNC_GPCK);
    clock_configure(clk_gpout0,
                    CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC, 0,
                    clock_get_hz(clk_sys),
                    AUD_CLOCK_FREQUENCY_KHZ * 1000);
#endif

    // Configure SPI communication
    spi_init(AUD_SPI, AUD_BAUDRATE_HZ);
    spi_set_format(AUD_SPI, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(AUD_SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(AUD_SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(AUD_SPI_TX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(AUD_SPI_CS_PIN, GPIO_FUNC_SPI);

    aud_i2s_pio_init();
}

static void aud_dump_registers(void)
{
    printf("\nSD-1 .0 .1 .2 .3 .4 .5 .6 .7 .8 .9 .A .B .C .D .E .F");
    for (uint8_t i = 0; i <= 80; ++i)
    {
        ;
        if (i % 0x10 == 0)
            printf("\n[%02X]", i);
        printf(" %02X", aud_read_register(i));
    }
}

void aud_task(void)
{
}

void aud_print_status(void)
{
    // aud_dump_registers();
}
