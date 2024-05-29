#include "hardware/interp.h"

#include "sys/std.h"
#include <stdint.h>
#include <stdio.h>

#include "cgia.h"
#include "cgia_palette.h"
#include "tmds_encode_cgia.h"

#include "veto-the_mill.h"

#include "sys/out.h"

static const uint32_t __scratch_x("tmds_table") tmds_table[] = {
#include "tmds_table.h"
};

#define PALETTE_WORDS 4
uint32_t __attribute__((aligned(4))) cgia_palette[CGIA_COLORS_NUM * PALETTE_WORDS];

#define DISPLAY_WIDTH_PIXELS   (FRAME_WIDTH / 2)
#define DISPLAY_BORDER_COLUMNS 4

#define FRAME_CHARS (DISPLAY_WIDTH_PIXELS / 8)
uint8_t __attribute__((aligned(4))) screen[FRAME_CHARS * 8];
uint8_t __attribute__((aligned(4))) colour[FRAME_CHARS];
uint8_t __attribute__((aligned(4))) backgr[FRAME_CHARS];

void init_palette()
{
    for (int i = 0; i < CGIA_COLORS_NUM; ++i)
    {
        uint32_t blue = (cgia_rgb_palette[i] & 0x0000ff) >> 0;
        uint32_t green = (cgia_rgb_palette[i] & 0x00ff00) >> 8;
        uint32_t red = (cgia_rgb_palette[i] & 0xff0000) >> 16;

        // FIXME: these should NOT be cut down to 6 bpp (>> 2) - generate proper symbols!
        cgia_palette[i * PALETTE_WORDS + 0] = tmds_table[blue >> 2];
        cgia_palette[i * PALETTE_WORDS + 1] = tmds_table[green >> 2];
        cgia_palette[i * PALETTE_WORDS + 2] = tmds_table[red >> 2];
        cgia_palette[i * PALETTE_WORDS + 3] = 0;
    }
}

static volatile struct registers_t
{
    uint16_t memory_scan;
    uint16_t color_scan;
    uint8_t border_color;
    uint8_t background_color;
    uint8_t row_height;
} __attribute__((aligned(4))) registers;

static volatile uint frame = 0;

void cgia_init(void)
{
    init_palette();

    // TODO: fill with 0s
    for (int i = 0; i < FRAME_CHARS * 8; ++i)
    {
        screen[i] = i & 0xff;
    }
    for (int i = 0; i < FRAME_CHARS; ++i)
    {
        colour[i] = i % CGIA_COLORS_NUM;
        backgr[i] = (127 - i) % CGIA_COLORS_NUM;
    }

    registers.border_color = 1;
    registers.background_color = 0;
    registers.row_height = 8; // FIXME: 0 should mean 1, 255 should mean 256
}

void cgia_core1_init(void)
{
    interp_config cfg = interp_default_config();
    interp_config_set_add_raw(&cfg, true);
    interp_set_config(interp0, 0, &cfg);
    interp_set_config(interp0, 1, &cfg);
    interp_set_config(interp1, 0, &cfg);
    interp_set_config(interp1, 1, &cfg);
    interp_set_base(interp1, 0, 1);
    interp_set_base(interp1, 1, 1);
}

void __not_in_flash_func(cgia_render)(uint y, uint32_t *tmdsbuf)
{
    uint32_t *p = tmdsbuf;

    if (y == 0)
    {
        ++frame;

        // if (frame % 10 == 0)
        // {
        //     registers.border_color = (registers.border_color + 1) & 0b01111111;
        // }
        // if (frame % 17 == 0)
        // {
        //     registers.background_color = (registers.background_color - 1) & 0b01111111;
        // }
    }

    if (y < 24 || y >= (24 + 192))
    {
        p = tmds_encode_border(p, registers.border_color, DISPLAY_WIDTH_PIXELS / 8);
    }
    // else if (y >= 48 && y < (48 + 128))
    // {
    //     uint8_t column_stride = registers.row_height;
    //     interp_set_base(interp0, 0, column_stride);
    //     uint8_t row_offset = y % registers.row_height;
    //     interp_set_accumulator(interp0, 0, (uintptr_t)screen - column_stride + row_offset);
    //     interp_set_accumulator(interp1, 0, (uintptr_t)colour - 1);
    //     interp_set_accumulator(interp1, 1, (uintptr_t)backgr - 1);
    //     p = tmds_encode_mode_3_shared(p, FRAME_WIDTH, &registers.background_color);
    // }
    else
    {
        p = tmds_encode_border(p, registers.border_color, DISPLAY_BORDER_COLUMNS);

        uint8_t row_height = registers.row_height;
        interp_set_base(interp0, 0, row_height);
        uint8_t offset = y - 24;
        uint8_t row = offset / row_height;
        uint8_t row_offset = offset % row_height;
        interp_set_accumulator(interp0, 0, (uintptr_t)bitmap_data - row_height + row * 40 * 8 + row_offset);
        interp_set_accumulator(interp1, 0, (uintptr_t)colour_data - 1 + row * 40);
        interp_set_accumulator(interp1, 1, (uintptr_t)background_data - 1 + row * 40);
        // p = tmds_encode_mode_3_shared(p, FRAME_WIDTH - DISPLAY_BORDER_COLUMNS * 8 * 2 * 2, &registers.background_color);
        p = tmds_encode_mode_3_mapped(p, FRAME_WIDTH - DISPLAY_BORDER_COLUMNS * 8 * 2 * 2);

        p = tmds_encode_border(p, registers.border_color, DISPLAY_BORDER_COLUMNS);
    }
}

void cgia_task(void)
{
}
