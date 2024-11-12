/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"

#include "main.h"
#include "out.h"

#include <limits.h>
#include <stdio.h>

/** https://retrocomputing.stackexchange.com/a/13872
> In standard bitmap mode the C64 outputs 320 pixels in 40µs.
> The visible portion of a line is ~52µs; in 60Hz regions ~240 lines
> are considered 'visible', but in PAL regions it's ~288 lines.
> So if there were no borders, there'd be around 52/40*320 = 416 pixels
> across the visible portion of a line.
*/

// ANTIC generates 32/40/48 column text mode => max 256/320/384 px map mode
// ANTIC supports up to 240 Display List instructions
// With pixel-doubling this gives 768x480 mode, which has nice 16:10 aspect ratio
// My TV recognizes this as 480p60 :-D
// Timings computed using https://tomverbeure.github.io/video_timings_calculator
// Back porches adjusted to fit 26.6MHz pixel clock
// #define MODE_H_SYNC_POLARITY 0
// #define MODE_H_FRONT_PORCH   24
// #define MODE_H_SYNC_WIDTH    72
// #define MODE_H_BACK_PORCH    96
// #define MODE_H_ACTIVE_PIXELS 768
// #define MODE_V_SYNC_POLARITY 1
// #define MODE_V_FRONT_PORCH   3
// #define MODE_V_SYNC_WIDTH    6
// #define MODE_V_BACK_PORCH    11
// #define MODE_V_ACTIVE_LINES  480
// #define MODE_BIT_CLK_KHZ     400000

// Reduced Blanking - Back porches adjusted to fit 26.6MHz pixel clock
#define MODE_H_SYNC_POLARITY 1
#define MODE_H_FRONT_PORCH   48
#define MODE_H_SYNC_WIDTH    32
#define MODE_H_BACK_PORCH    48
#define MODE_H_ACTIVE_PIXELS 768
#define MODE_V_SYNC_POLARITY 0
#define MODE_V_FRONT_PORCH   3
#define MODE_V_SYNC_WIDTH    6
#define MODE_V_BACK_PORCH    6
#define MODE_V_ACTIVE_LINES  480
#define MODE_BIT_CLK_KHZ     266000

#define FB_H_REPEAT 2
#define FB_V_REPEAT 2

// ----------------------------------------------------------------------------
// RGB line buffers
#define RGB_LINE_BUFFERS 2
static uint32_t linebuffer[MODE_H_ACTIVE_PIXELS * RGB_LINE_BUFFERS];

static uint a_scanline = 0;
static uint cur_scanline = UINT_MAX;
static io_rw_32 cur_line_ptr;
static uint gen_scanline = UINT_MAX;
static io_rw_32 gen_line_ptr;

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

// #define MODE_H_SYNC_POLARITY 0
// #define MODE_H_FRONT_PORCH   16
// #define MODE_H_SYNC_WIDTH    96
// #define MODE_H_BACK_PORCH    48
// #define MODE_H_ACTIVE_PIXELS 640

// #define MODE_V_SYNC_POLARITY 0
// #define MODE_V_FRONT_PORCH   10
// #define MODE_V_SYNC_WIDTH    2
// #define MODE_V_BACK_PORCH    33
// #define MODE_V_ACTIVE_LINES  480

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

#define DMACH_PING 0
#define DMACH_PONG 1

// First we ping. Then we pong. Then... we ping again.
static bool dma_pong = false;

// A ping and a pong are cued up initially, so the first time we enter this
// handler it is to cue up the second ping after the first ping has completed.
// This is the third scanline overall (-> =2 because zero-based).
static uint v_scanline = 2;

// During the vertical active period, we take two IRQs per scanline: one to
// post the command list, and another to post the pixels.
static bool vactive_cmdlist_posted = false;

void __scratch_x("") dma_irq_handler(void)
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
        ch->transfer_count = MODE_H_ACTIVE_PIXELS / FB_H_REPEAT;
        vactive_cmdlist_posted = false;

        ++a_scanline;
    }

    if (!vactive_cmdlist_posted)
    {
        v_scanline = (v_scanline + 1) % MODE_V_TOTAL_LINES;
    }
}

void __not_in_flash_func(out_core1_main)(void)
{
    // Configure HSTX's TMDS encoder for RGB888
    hstx_ctrl_hw->expand_tmds = 7 << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB // R
                                | 16 << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB
                                | 7 << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB // G
                                | 8 << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB
                                | 7 << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB // B
                                | 0 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;

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

    // Assign clock pair to two neighbouring pins:
    hstx_ctrl_hw->bit[2] = HSTX_CTRL_BIT0_CLK_BITS;
    hstx_ctrl_hw->bit[3] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
    for (uint lane = 0; lane < 3; ++lane)
    {
        // For each TMDS lane, assign it to the correct GPIO pair based on the
        // desired pinout:
        static const int lane_to_output_bit[3] = {0, 6, 4};
        int bit = lane_to_output_bit[lane];
        // Output even bits during first half of each HSTX cycle, and odd bits
        // during second half. The shifter advances by two bits each cycle.
        uint32_t lane_data_sel_bits = (lane * 10) << HSTX_CTRL_BIT0_SEL_P_LSB
                                      | (lane * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB;
        // The two halves of each pair get identical data, but one pin is inverted.
        hstx_ctrl_hw->bit[bit] = lane_data_sel_bits;
        hstx_ctrl_hw->bit[bit + 1] = lane_data_sel_bits | HSTX_CTRL_BIT0_INV_BITS;
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

    gen_line_ptr = (uintptr_t)(linebuffer);
    cur_line_ptr = (uintptr_t)(linebuffer + MODE_H_ACTIVE_PIXELS);

    while (true)
    {
        static uint active_scanline = UINT_MAX;
        if (a_scanline != active_scanline)
        {
            active_scanline = a_scanline;

            uint active_raster = active_scanline / FB_V_REPEAT;
            uint generated_raster = gen_scanline / FB_V_REPEAT;
            if (generated_raster != active_raster)
            {
                // TODO: call CGIA generator for active_raster
                {
                    uint32_t *fill = (uint32_t *)gen_line_ptr;
                    for (unsigned i = 0; i < MODE_H_ACTIVE_PIXELS / FB_H_REPEAT; ++i)
                    {
                        *fill++ = (i & 0xff) | (active_raster & 0xff) << 8 | (((i + active_raster) / 2) & 0xff) << 16;
                        *((uint32_t *)gen_line_ptr + active_raster) = 0xffffff;
                    }
                }
                gen_scanline = active_raster * FB_V_REPEAT;
            }
        }

        __wfi();
    }
    __builtin_unreachable();
}

void out_reclock(void)
{
    clock_configure(clk_hstx,
                    CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS,
                    0,
                    clock_get_hz(clk_sys),
                    MODE_BIT_CLK_KHZ * KHZ / 2 /*DDR*/);
}

void out_init(void)
{
    vreg_set_voltage(MAIN_VREG_VSEL);
    sleep_ms(10);
    set_sys_clock_khz(MAIN_SYS_CLOCK_KHZ, true);
    main_reclock();

    multicore_launch_core1(out_core1_main);
}

void out_print_status(void)
{
    printf("CORE: %.1fMHz\n", (float)(clock_get_hz(clk_sys)) / MHZ);
    printf("DVI: %dx%d@24bpp\n", MODE_H_ACTIVE_PIXELS, MODE_V_ACTIVE_LINES);
}
