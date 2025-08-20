/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pll.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"
#include "pico/multicore.h"

#include "cgia/cgia.h"
#include "main.h"
#include "out.h"
#include "term/term.h"

#include <limits.h>
#include <stdio.h>

// ----------------------------------------------------------------------------
#define LINE_BUFFER_PADDING (-SCHAR_MIN) // maximum scroll of signed 8 bit
// RGB line buffers
#define RGB_LINE_BUFFERS    2
#define RGB_LINE_BUFFER_LEN (MODE_H_ACTIVE_PIXELS + 2 * LINE_BUFFER_PADDING)
static uint32_t linebuffer[RGB_LINE_BUFFER_LEN * RGB_LINE_BUFFERS];

static uint a_scanline = 0;
static uint cur_scanline = UINT_MAX;
static io_rw_32 cur_line_ptr;
static uint gen_scanline = UINT_MAX;
static io_rw_32 gen_line_ptr;

static bool trigger_vbl = false;

static uint pixel_doubling = FB_H_REPEAT ? 1 : 0;

// ----------------------------------------------------------------------------
// DVI constants

#define TMDS_CTRL_00 0x354u
#define TMDS_CTRL_01 0x0abu
#define TMDS_CTRL_10 0x154u
#define TMDS_CTRL_11 0x2abu

#define SYNC_V0_H0 (TMDS_CTRL_00 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V0_H1 (TMDS_CTRL_01 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H0 (TMDS_CTRL_10 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H1 (TMDS_CTRL_11 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))

#define MODE_H_TOTAL_PIXELS ( \
    MODE_H_FRONT_PORCH + MODE_H_SYNC_WIDTH + MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS)
#define MODE_V_TOTAL_LINES ( \
    MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + MODE_V_BACK_PORCH + MODE_V_ACTIVE_LINES)

#define HSTX_CMD_RAW         (0x0u << 12)
#define HSTX_CMD_RAW_REPEAT  (0x1u << 12)
#define HSTX_CMD_TMDS        (0x2u << 12)
#define HSTX_CMD_TMDS_REPEAT (0x3u << 12)
#define HSTX_CMD_NOP         (0xfu << 12)

// ----------------------------------------------------------------------------
// HSTX command lists

// Lists are padded with NOPs to be >= HSTX FIFO size, to avoid DMA rapidly
// pingponging and tripping up the IRQs.

static uint32_t vblank_line_vsync_off[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V1_H0,
    HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS),
    SYNC_V1_H1,
    HSTX_CMD_NOP};

static uint32_t vblank_line_vsync_on[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V0_H1,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V0_H0,
    HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS),
    SYNC_V0_H1,
    HSTX_CMD_NOP};

static uint32_t vactive_line[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_NOP,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V1_H0,
    HSTX_CMD_NOP,
    HSTX_CMD_RAW_REPEAT | MODE_H_BACK_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_TMDS | MODE_H_ACTIVE_PIXELS};

// ----------------------------------------------------------------------------
// DMA logic

#define DMACH_PING DVI_DMACH_PING
#define DMACH_PONG DVI_DMACH_PONG

// First we ping. Then we pong. Then... we ping again.
static bool dma_pong = false;

// A ping and a pong are cued up initially, so the first time we enter this
// handler it is to cue up the second ping after the first ping has completed.
// This is the third scanline overall (-> =2 because zero-based).
static uint v_scanline = 2;

// During the vertical active period, we take two IRQs per scanline: one to
// post the command list, and another to post the pixels.
static bool vactive_cmdlist_posted = false;

void __isr
    __scratch_y("")
        dma_irq_handler(void)
{
    // dma_pong indicates the channel that just finished, which is the one
    // we're about to reload.
    uint ch_num = dma_pong ? DMACH_PONG : DMACH_PING;
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    dma_hw->intr = 1u << ch_num;
    dma_pong = !dma_pong;

    if (v_scanline >= MODE_V_FRONT_PORCH && v_scanline < (MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH))
    {
        ch->read_addr = (uintptr_t)vblank_line_vsync_on;
        ch->transfer_count = count_of(vblank_line_vsync_on);

        a_scanline = 0;

        if (!main_active() && pixel_doubling)
        {
            pixel_doubling = 0;
            hstx_ctrl_hw->expand_shift = 1 << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB
                                         | 0 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB
                                         | 1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB
                                         | 0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;
        }
        else if (main_active() && !pixel_doubling)
        {
            pixel_doubling = 1;
            hstx_ctrl_hw->expand_shift = 2 << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB
                                         | 0 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB
                                         | 1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB
                                         | 0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;
        }
    }
    else if (v_scanline < MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + MODE_V_BACK_PORCH)
    {
        ch->read_addr = (uintptr_t)vblank_line_vsync_off;
        ch->transfer_count = count_of(vblank_line_vsync_off);

        a_scanline = 0;
    }
    else if (!vactive_cmdlist_posted)
    {
        ch->read_addr = (uintptr_t)vactive_line;
        ch->transfer_count = count_of(vactive_line);
        vactive_cmdlist_posted = true;
    }
    else
    {
        if (a_scanline >= gen_scanline && cur_scanline != gen_scanline)
        {
            cur_scanline = gen_scanline;
            io_rw_32 ptr = cur_line_ptr;
            cur_line_ptr = gen_line_ptr;
            gen_line_ptr = ptr;
        }
        ch->read_addr = cur_line_ptr;
        ch->transfer_count = MODE_H_ACTIVE_PIXELS >> pixel_doubling;
        vactive_cmdlist_posted = false;

        ++a_scanline;
    }

    if (!vactive_cmdlist_posted)
    {
        v_scanline += 1;
        if (v_scanline >= MODE_V_TOTAL_LINES)
        {
            v_scanline = 0;

            trigger_vbl = true;
        }
    }
}

void __not_in_flash_func(out_core1_main)(void)
{
    // Configure HSTX's TMDS encoder for RGB888
    hstx_ctrl_hw->expand_tmds = 7 << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB // R
                                | 0 << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB
                                | 7 << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB // G
                                | 8 << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB
                                | 7 << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB // B
                                | 16 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;

    // Pixels (TMDS) come in 3x 8-bit RGB + 1 byte padding.
    // Control symbols (RAW) are an entire 32-bit word.
    hstx_ctrl_hw->expand_shift = FB_H_REPEAT << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB
                                 | 0 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB
                                 | 1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB
                                 | 0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;

    // Serial output config: clock period of 5 cycles, pop from command
    // expander every 5 cycles, shift the output shiftreg by 2 every cycle.
    hstx_ctrl_hw->csr = 0;
    hstx_ctrl_hw->csr = HSTX_CTRL_CSR_EXPAND_EN_BITS
                        | 5u << HSTX_CTRL_CSR_CLKDIV_LSB
                        | 5u << HSTX_CTRL_CSR_N_SHIFTS_LSB
                        | 2u << HSTX_CTRL_CSR_SHIFT_LSB
                        | HSTX_CTRL_CSR_EN_BITS;

    // Note we are leaving the HSTX clock at the SDK default of 125 MHz; since
    // we shift out two bits per HSTX clock cycle, this gives us an output of
    // 250 Mbps, which is very close to the bit clock for 480p 60Hz (252 MHz).
    // If we want the exact rate then we'll have to reconfigure PLLs.

    // HSTX outputs 0 through 7 appear on GPIO 12 through 19.
    // Pinout on Pico DVI sock:
    //
    //   GP12 D0+  GP13 D0-
    //   GP14 CK+  GP15 CK-
    //   GP16 D2+  GP17 D2-
    //   GP18 D1+  GP19 D1-
    //
    // Pinout on X65 board:
    //
    //   GP12 CK-  GP13 CK+
    //   GP14 D0-  GP15 D0+
    //   GP16 D1-  GP17 D1+
    //   GP18 D2-  GP19 D2+

    // Assign clock pair to two neighbouring pins:
    hstx_ctrl_hw->bit[1] = HSTX_CTRL_BIT0_CLK_BITS;
    hstx_ctrl_hw->bit[0] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
    for (uint lane = 0; lane < 3; ++lane)
    {
        // For each TMDS lane, assign it to the correct GPIO pair based on the
        // desired pinout:
        static const int lane_to_output_bit[3] = {2, 4, 6};
        int bit = lane_to_output_bit[lane];
        // Output even bits during first half of each HSTX cycle, and odd bits
        // during second half. The shifter advances by two bits each cycle.
        uint32_t lane_data_sel_bits = (lane * 10) << HSTX_CTRL_BIT0_SEL_P_LSB
                                      | (lane * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB;
        // The two halves of each pair get identical data, but one pin is inverted.
        hstx_ctrl_hw->bit[bit + 1] = lane_data_sel_bits;
        hstx_ctrl_hw->bit[bit] = lane_data_sel_bits | HSTX_CTRL_BIT0_INV_BITS;
    }

    for (int i = 12; i <= 19; ++i)
    {
        gpio_set_function(i, 0); // HSTX
    }

    // Both channels are set up identically, to transfer a whole scanline and
    // then chain to the opposite channel. Each time a channel finishes, we
    // reconfigure the one that just finished, meanwhile the opposite channel
    // is already making progress.
    dma_channel_config c;
    c = dma_channel_get_default_config(DMACH_PING);
    channel_config_set_chain_to(&c, DMACH_PONG);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(
        DMACH_PING,
        &c,
        &hstx_fifo_hw->fifo,
        vblank_line_vsync_off,
        count_of(vblank_line_vsync_off),
        false);
    c = dma_channel_get_default_config(DMACH_PONG);
    channel_config_set_chain_to(&c, DMACH_PING);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(
        DMACH_PONG,
        &c,
        &hstx_fifo_hw->fifo,
        vblank_line_vsync_off,
        count_of(vblank_line_vsync_off),
        false);

    dma_hw->ints0 = (1u << DMACH_PING) | (1u << DMACH_PONG);
    dma_hw->inte0 = (1u << DMACH_PING) | (1u << DMACH_PONG);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;
    hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);

    dma_channel_start(DMACH_PING);

    gen_line_ptr = (uintptr_t)(linebuffer + LINE_BUFFER_PADDING);
    cur_line_ptr = (uintptr_t)(linebuffer + LINE_BUFFER_PADDING + RGB_LINE_BUFFER_LEN);

    while (true)
    {
        if (trigger_vbl)
        {
            cgia_vbi();
            trigger_vbl = false;
        }

        static uint active_scanline = UINT_MAX;
        if (a_scanline != active_scanline)
        {
            active_scanline = a_scanline;

            uint active_raster = active_scanline / FB_V_REPEAT;
            uint generated_raster = gen_scanline / FB_V_REPEAT;
            if (generated_raster != active_raster)
            {
                if (pixel_doubling)
                {
                    cgia_render((uint16_t)active_raster, (uint32_t *)gen_line_ptr);
                }
                else
                {
                    term_render(active_raster, (uint32_t *)gen_line_ptr);
                }
                gen_scanline = active_scanline;
            }
        }

        __wfi(); // wait for interrupt
    }
    __builtin_unreachable();
}

// * 10 : TMDS symbol bits
// / 2  : DDR - two bits per clock on each edge
#define OUT_HSTX_HZ (MODE_V_FREQ_HZ * MODE_V_TOTAL_LINES * MODE_H_TOTAL_PIXELS * 10 / 2)

void out_post_reclock(void)
{
    // This function is called after the USB clock has been switched to use PLL_SYS.
    // Now we can re-use the PLL_USB to generate the HSTX clock, with perfect 60Hz rate.
    pll_init(pll_usb, 1, OUT_HSTX_HZ * 5 * 2, 5, 2);

    clock_configure(clk_hstx,
                    0,
                    CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    OUT_HSTX_HZ,
                    OUT_HSTX_HZ);
}

void out_init(void)
{
    // Begin by configuring HSTX clock from system clock.
    // This is close, but not exact (HSTX clock divider lacks fractional divisor)
    clock_configure(clk_hstx,
                    0,
                    CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS,
                    clock_get_hz(clk_sys),
                    OUT_HSTX_HZ);

    multicore_launch_core1(out_core1_main);
}

void out_print_status(void)
{
    const float clk = (float)(clock_get_hz(clk_sys));
    printf("CORE: %.1fMHz\n", clk / MHZ);

    const float hstx_div = (float)(clocks_hw->clk[clk_hstx].div >> 16);
    const float refresh_hz = OUT_HSTX_HZ / hstx_div * 2 / 10 / MODE_H_TOTAL_PIXELS / MODE_V_TOTAL_LINES;
    printf("DVI : %dx%d@%.1fHz/24bpp\n", MODE_H_ACTIVE_PIXELS, MODE_V_ACTIVE_LINES, refresh_hz);

#if 0
    uint f_pll_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_PLL_SYS_CLKSRC_PRIMARY);
    uint f_pll_usb = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_PLL_USB_CLKSRC_PRIMARY);
    uint f_rosc = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_ROSC_CLKSRC);
    uint f_clk_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
    uint f_clk_peri = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_PERI);
    uint f_clk_usb = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_USB);
    uint f_clk_adc = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_ADC);
    uint f_clk_hstx = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_HSTX);

    printf("pll_sys  = %dkHz\n", f_pll_sys);
    printf("pll_usb  = %dkHz\n", f_pll_usb);
    printf("rosc     = %dkHz\n", f_rosc);
    printf("clk_sys  = %dkHz\n", f_clk_sys);
    printf("clk_peri = %dkHz\n", f_clk_peri);
    printf("clk_usb  = %dkHz\n", f_clk_usb);
    printf("clk_adc  = %dkHz\n", f_clk_adc);
    printf("clk_hstx = %dkHz\n", f_clk_hstx);

    uint32_t ctrl = clocks_hw->clk[clk_hstx].ctrl;
    uint32_t div = clocks_hw->clk[clk_hstx].div;
    printf("HSTX CTRL: 0x%08x, DIV: 0x%08x\n", ctrl, div);
#endif
}
