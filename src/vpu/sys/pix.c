/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/pix.h"
#include "../pix.h"
#include "hw.h"
#include "pix.pio.h"
#include "ria/hw.h"
#include "term/font.h"
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/pio.h>
#include <pico/stdlib.h>
#include <stdio.h>
#include <string.h>

#define PIX_CH0_XREGS_MAX 8
static uint16_t xregs[PIX_CH0_XREGS_MAX];

volatile const uint8_t xram[0x10000] __attribute__((aligned(0x10000)));

static void pix_ack(void)
{
}
static void pix_nak(void)
{
}

static bool pix_ch0_xreg(uint8_t addr, uint16_t word)
{
    if (addr < PIX_CH0_XREGS_MAX)
        xregs[addr] = word;
    if (addr == 0) // CANVAS
    {
        // if (vga_xreg_canvas(xregs))
        //     pix_ack();
        // else
        pix_nak();
    }
    if (addr == 1) // MODE
    {
        // if (main_prog(xregs))
        //     pix_ack();
        // else
        pix_nak();
    }
    if (addr == 0 || addr == 1)
    {
        memset(&xregs, 0, sizeof(xregs));
        return true;
    }
    return false;
}

static bool pix_ch15_xreg(uint8_t addr, uint16_t word)
{
    switch (addr)
    {
    case 0x00: // DISPLAY
        // Also performs a reset.
        // vga_xreg_canvas(NULL);
        // vga_set_display(word);
        memset(&xregs, 0, sizeof(xregs));
        return true;
    case 0x01: // CODE_PAGE
        font_set_code_page(word);
        return true;
    case 0x03: // UART_TX
        putchar_raw(word);
        return false;
    case 0x04: // BACKCHAN
        // ria_backchan(word);
        return false;
    }
    return false;
}

void pix_init(void)
{
    pio_set_gpio_base(PIX_PIO, 16);
    uint offset = pio_add_program(PIX_PIO, &pix_vpu_program);
    pio_sm_config config = pix_vpu_program_get_default_config(offset);
    const float clkdiv = (float)(clock_get_hz(clk_sys)) / (PIX_BUS_PIO_SPEED_KHZ * KHZ);
    sm_config_set_clkdiv(&config, clkdiv);
    sm_config_set_out_pin_base(&config, PIX_PIN_BASE);
    sm_config_set_out_pin_count(&config, 8);
    sm_config_set_in_pin_base(&config, PIX_PIN_BASE);
    sm_config_set_in_pin_count(&config, 8);
    sm_config_set_sideset_pin_base(&config, PIX_PIN_DTR);
    sm_config_set_jmp_pin(&config, PIX_PIN_SCK);
    sm_config_set_out_shift(&config, true, false, 0);
    sm_config_set_in_shift(&config, false, true, 8);
    for (int i = 0; i < PIX_PINS_USED; i++)
        pio_gpio_init(PIX_PIO, PIX_PIN_BASE + i);
    pio_sm_init(PIX_PIO, PIX_SM, offset, &config);
    pio_sm_set_consecutive_pindirs(PIX_PIO, PIX_SM, PIX_PIN_BASE, PIX_PINS_USED, false);
    pio_sm_set_consecutive_pindirs(PIX_PIO, PIX_SM, PIX_PIN_DTR, 1, true);
    // pio_sm_put(PIX_PIO, PIX_SM, PIX_MESSAGE(PIX_DEVICE_IDLE, 0, 0, 0));
    pio_sm_set_enabled(PIX_PIO, PIX_SM, true);
}

void pix_task(void)
{
    while (!pio_sm_is_rx_fifo_empty(PIX_PIO, PIX_SM))
    {
        uint32_t raw = pio_sm_get(PIX_PIO, PIX_SM);
        printf(">>>: %08X\n", raw);
        pio_sm_put(PIX_PIO, PIX_SM, raw | 0x80); // echo back
        // uint8_t ch = (raw & 0x0F000000) >> 24;
        // uint8_t addr = (raw & 0x00FF0000) >> 16;
        // uint16_t word = raw & 0xFFFF;
        // // These return true on slow operations to
        // // allow us to stay greedy on fast ones.
        // if (ch == 0 && pix_ch0_xreg(addr, word))
        //     break;
        // if (ch == 15 && pix_ch15_xreg(addr, word))
        //     break;
    }
}
