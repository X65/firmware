/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "out.h"

#include "cgia/cgia.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "main.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "term/term.h"

#include "dvi.h"
#include "dvi_serialiser.h"

#define __dvi_const(x) __not_in_flash_func(x)

// Pi Pico VPU DVI
static const struct dvi_serialiser_cfg x65_dvi_cfg = {
    .pio = DVI_DEFAULT_PIO_INST,
    .sm_tmds = {DVI_TMDS0_SM, DVI_TMDS1_SM, DVI_TMDS2_SM},
    .pins_tmds = {DVI_TMDS0_PIN, DVI_TMDS1_PIN, DVI_TMDS2_PIN},
    .pins_clk = DVI_CLK_PIN,
    .invert_diffpairs = false,
};

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
const struct dvi_timing __dvi_const(dvi_timing_768x480p_60hz) = {
    .h_sync_polarity = true,
    .h_front_porch = 48,
    .h_sync_width = 32,
    .h_back_porch = 48,
    .h_active_pixels = 768,

    .v_sync_polarity = false,
    .v_front_porch = 3,
    .v_sync_width = 6,
    .v_back_porch = 6,
    .v_active_lines = 480,

    .bit_clk_khz = 266000,
};

// DVDD 1.2V (1.1V seems ok too)
#define VREG_VSEL  VREG_VOLTAGE_1_20
#define DVI_TIMING dvi_timing_768x480p_60hz

struct dvi_inst dvi0;

void __not_in_flash_func(out_core1_main)()
{
    // CGIA needs to initialize this core interpolators
    cgia_core1_init();

    dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
    dvi_start(&dvi0);
    while (true)
    {
        for (uint y = 0; y < FRAME_HEIGHT; ++y)
        {
            uint32_t *tmdsbuf;
            queue_remove_blocking(&dvi0.q_tmds_free, &tmdsbuf);
            cgia_render(y, tmdsbuf);
            queue_add_blocking(&dvi0.q_tmds_valid, &tmdsbuf);
        }
    }
    __builtin_unreachable();
}

void out_task(void)
{
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

    hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
    multicore_launch_core1(out_core1_main);
}
