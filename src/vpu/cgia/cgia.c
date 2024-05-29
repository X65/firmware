#include "hardware/interp.h"

#include "sys/std.h"
#include <stdint.h>
#include <stdio.h>

#include "cgia.h"
#include "tmds_encode_cgia.h"

#include "sys/out.h"

static const uint32_t __scratch_x("tmds_table") tmds_table[] = {
#include "tmds_table.h"
};

/** Generated using script (see colors.html) */
uint32_t cgia_rgb_palette[CGIA_COLORS_NUM] = {
    0x00000000, 0x00242424, 0x00494949, 0x006d6d6d, //
    0x00929292, 0x00b6b6b6, 0x00dbdbdb, 0x00ffffff, //
    0x00440505, 0x006f0e0e, 0x008b2626, 0x00a04545, //
    0x00ba5f5f, 0x00d97474, 0x00f19090, 0x00fabbbb, //
    0x00441e05, 0x006f350e, 0x008b4e26, 0x00a06945, //
    0x00ba835f, 0x00d99c74, 0x00f1b790, 0x00fad4bb, //
    0x00443805, 0x006f5c0e, 0x008b7726, 0x00a08e45, //
    0x00baa85f, 0x00d9c574, 0x00f1de90, 0x00faeebb, //
    0x00384405, 0x005c6f0e, 0x00778b26, 0x008ea045, //
    0x00a8ba5f, 0x00c5d974, 0x00def190, 0x00eefabb, //
    0x001e4405, 0x00356f0e, 0x004e8b26, 0x0069a045, //
    0x0083ba5f, 0x009cd974, 0x00b7f190, 0x00d4fabb, //
    0x00054405, 0x000e6f0e, 0x00268b26, 0x0045a045, //
    0x005fba5f, 0x0074d974, 0x0090f190, 0x00bbfabb, //
    0x0005441e, 0x000e6f35, 0x00268b4e, 0x0045a069, //
    0x005fba83, 0x0074d99c, 0x0090f1b7, 0x00bbfad4, //
    0x00054438, 0x000e6f5c, 0x00268b77, 0x0045a08e, //
    0x005fbaa8, 0x0074d9c5, 0x0090f1de, 0x00bbfaee, //
    0x00053844, 0x000e5c6f, 0x0026778b, 0x00458ea0, //
    0x005fa8ba, 0x0074c5d9, 0x0090def1, 0x00bbeefa, //
    0x00051e44, 0x000e356f, 0x00264e8b, 0x004569a0, //
    0x005f83ba, 0x00749cd9, 0x0090b7f1, 0x00bbd4fa, //
    0x00050544, 0x000e0e6f, 0x0026268b, 0x004545a0, //
    0x005f5fba, 0x007474d9, 0x009090f1, 0x00bbbbfa, //
    0x001e0544, 0x00350e6f, 0x004e268b, 0x006945a0, //
    0x00835fba, 0x009c74d9, 0x00b790f1, 0x00d4bbfa, //
    0x00380544, 0x005c0e6f, 0x0077268b, 0x008e45a0, //
    0x00a85fba, 0x00c574d9, 0x00de90f1, 0x00eebbfa, //
    0x00440538, 0x006f0e5c, 0x008b2677, 0x00a0458e, //
    0x00ba5fa8, 0x00d974c5, 0x00f190de, 0x00fabbee, //
    0x0044051e, 0x006f0e35, 0x008b264e, 0x00a04569, //
    0x00ba5f83, 0x00d9749c, 0x00f190b7, 0x00fabbd4, //
};
#define PALETTE_BITS 7
_Static_assert(CGIA_COLORS_NUM == 1 << PALETTE_BITS, "Incorrect PALETTE_BITS");

#define PALETTE_WORDS 4
uint32_t __attribute__((aligned(4))) cgia_palette[CGIA_COLORS_NUM * PALETTE_WORDS];

#define DISPLAY_WIDTH_PIXELS   (FRAME_WIDTH / 2)
#define DISPLAY_BORDER_COLUMNS 4

#define FRAME_CHARS (DISPLAY_WIDTH_PIXELS / 8)
uint8_t __attribute__((aligned(4))) screen[FRAME_CHARS];
uint8_t __attribute__((aligned(4))) colour[FRAME_CHARS];
uint8_t __attribute__((aligned(4))) backgr[FRAME_CHARS];

void init_palette()
{
    for (int i = 0; i < CGIA_COLORS_NUM; ++i)
    {
        // FIXME: these should NOT be cut down to 6 bpp - generate proper symbols!
        uint32_t blue = (cgia_rgb_palette[i] & 0x0000ff) >> 0;
        uint32_t green = (cgia_rgb_palette[i] & 0x00ff00) >> 8;
        uint32_t red = (cgia_rgb_palette[i] & 0xff0000) >> 16;

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
    uint8_t row_height;
} __attribute__((aligned(4))) registers;

static volatile uint frame = 0;

void cgia_init(void)
{
    init_palette();

    // TODO: fill with 0s
    for (int i = 0; i < FRAME_CHARS; ++i)
    {
        screen[i] = i & 0xff;
        colour[i] = i % CGIA_COLORS_NUM;
        backgr[i] = (127 - i) % CGIA_COLORS_NUM;
    }

    registers.border_color = 1;
    registers.row_height = 1;
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

        if (frame % 10 == 0)
        {
            registers.border_color = (registers.border_color + 1) & 0b01111111;
        }
    }

    if (y < 24 || y >= (24 + 192))
    {
        p = tmds_encode_border(p, registers.border_color, DISPLAY_WIDTH_PIXELS / 8);
    }
    else if (y >= 48 && y < (48 + 128))
    {
        uint32_t *buf = p;

        uint8_t column_stride = registers.row_height;
        interp_set_accumulator(interp0, 0, (uintptr_t)screen - column_stride);
        interp_set_base(interp0, 0, column_stride);
        interp_set_accumulator(interp1, 0, (uintptr_t)colour - 1);
        interp_set_accumulator(interp1, 1, (uintptr_t)backgr - 1);
        p = tmds_encode_mode_3(p, screen, colour, FRAME_WIDTH);

        {
            static char printf_buffer[256];
            char *chr;
            if (y == 48 && frame == 60 * 5)
            {
                int count = (uint8_t *)p - (uint8_t *)buf;
                for (int i = 0; i < count * 3; ++i)
                {
                    if (i % count == 0)
                    {
                        sprintf(printf_buffer, "<%p\n\r", (uint8_t *)buf);
                        chr = printf_buffer;
                        while (*chr)
                        {
                            std_out_write(*chr);
                            chr++;
                        }
                    }
                    sprintf(printf_buffer, "%02x ", ((uint8_t *)buf)[i]);
                    chr = printf_buffer;
                    while (*chr)
                    {
                        std_out_write(*chr);
                        chr++;
                    }
                    if (i % 64 == 63)
                    {
                        std_out_write('\n');
                        std_out_write('\r');
                    }
                }
                std_out_write('\n');
                std_out_write('\r');
                sprintf(printf_buffer, ">%p = %d (0x%x)", (uint8_t *)p, count, count);
                chr = printf_buffer;
                while (*chr)
                {
                    std_out_write(*chr);
                    chr++;
                }
                std_out_write('\n');
                std_out_write('\r');

                for (int i = 0; i < 10; ++i)
                {
                    sprintf(printf_buffer, "%08x ", cgia_palette[i]);
                    chr = printf_buffer;
                    while (*chr)
                    {
                        std_out_write(*chr);
                        chr++;
                    }
                }
                std_out_write('\n');
                std_out_write('\r');
            }
        }
    }
    else
    {
        p = tmds_encode_border(p, registers.border_color, DISPLAY_BORDER_COLUMNS);

        uint8_t column_stride = registers.row_height;
        interp_set_accumulator(interp0, 0, (uintptr_t)screen - column_stride);
        interp_set_base(interp0, 0, column_stride);
        interp_set_accumulator(interp1, 0, (uintptr_t)colour - 1);
        interp_set_accumulator(interp1, 1, (uintptr_t)backgr - 1);
        p = tmds_encode_mode_3(p, screen, colour, FRAME_WIDTH - DISPLAY_BORDER_COLUMNS * 8 * 2 * 2);

        p = tmds_encode_border(p, registers.border_color, DISPLAY_BORDER_COLUMNS);
    }
}

void cgia_task(void)
{
}
