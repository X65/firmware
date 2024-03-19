/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "out.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "main.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <stdlib.h>

#include "dvi.h"
#include "dvi_serialiser.h"

#include "picodvi/software/assets/testcard_320x240_rgb565.h"

// Pi Pico VPU DVI
static const struct dvi_serialiser_cfg x65_dvi_cfg = {
    .pio = DVI_DEFAULT_PIO_INST,
    .sm_tmds = {DVI_TMDS0_SM, DVI_TMDS1_SM, DVI_TMDS2_SM},
    .pins_tmds = {DVI_TMDS0_PIN, DVI_TMDS1_PIN, DVI_TMDS2_PIN},
    .pins_clk = DVI_CLK_PIN,
    .invert_diffpairs = false};

// DVDD 1.2V (1.1V seems ok too)
#define FRAME_WIDTH  320
#define FRAME_HEIGHT 240
#define VREG_VSEL    VREG_VOLTAGE_1_20
#define DVI_TIMING   dvi_timing_640x480p_60hz

struct dvi_inst dvi0;

void out_core1_main()
{
    dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
    while (queue_is_empty(&dvi0.q_colour_valid))
        __wfe();
    dvi_start(&dvi0);
    dvi_scanbuf_main_16bpp(&dvi0);
}

void out_task(void)
{
    // Pass out pointers into our prepared image, discard the pointers when
    // returned to us. Use frame_ctr to scroll the image
    static uint frame_ctr = 0;

    static uint y = 0;
    static const uint16_t *scanline = NULL;

    if (!scanline)
    {
        uint y_scroll = (y + frame_ctr) % FRAME_HEIGHT;
        scanline = &((const uint16_t *)testcard_320x240)[y_scroll * FRAME_WIDTH];
        queue_add_blocking_u32(&dvi0.q_colour_valid, &scanline);

        if (++y >= FRAME_HEIGHT)
        {
            y = 0;
            ++frame_ctr;
        }
    }
    else
    {
        if (queue_try_remove_u32(&dvi0.q_colour_free, &scanline))
        {
            scanline = NULL;
        }
    }
}

void out_init(void)
{
    vreg_set_voltage(VREG_VSEL);
    sleep_ms(10);
    set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);
    main_reclock();

    dvi0.timing = &DVI_TIMING;
    dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
    dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

    // Core 1 will wait until it sees the first colour buffer, then start up the
    // DVI signalling.
    multicore_launch_core1(out_core1_main);
}
