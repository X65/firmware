#include "hardware/interp.h"

#include "sys/std.h"
#include <stdint.h>
#include <stdio.h>

#include "cgia.h"
#include "cgia_palette.h"
#include "tmds_encode_cgia.h"

#include "display_lists.h"
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
    uint8_t display_bank;
    uint8_t sprite_bank;

    uint16_t dl_offset;
    uint16_t memory_offset;
    uint16_t colour_offset;
    uint16_t backgr_offset;

    uint8_t sprite_enable;
    uint16_t sprite_offset[8];

    // TODO: change to DBANK(8) + DLIST(16) (DLISTL/DLISTH)
    uint8_t *display_list;
    uint8_t *memory_scan;
    uint8_t *colour_scan;
    uint8_t *backgr_scan;

    uint8_t border_color;
    uint8_t background_color;
    uint8_t row_height;
    uint8_t border_columns;
    bool transparent_background;
} __attribute__((aligned(4))) registers;

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

    // FIXME: these should be initialized by CPU Operating System
    registers.border_color = 1;
    registers.background_color = 0;
    registers.row_height = 7;
    registers.display_list = hires_mode_dl;
    registers.memory_scan = bitmap_data;
    registers.colour_scan = colour_data;
    registers.backgr_scan = background_data;

    // Config
    registers.transparent_background = false;
    registers.border_columns = 4;
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

#define MODE_BIT 0b00001000
#define DLI_BIT  0b10000000

void __not_in_flash_func(cgia_render)(uint y, uint32_t *tmdsbuf)
{
    static uint8_t row_line_count = 0;
    static bool wait_vbl = true;
#if 0
    { // DL debugger
        static int frame = 0;
        if (y == 0)
            ++frame;
        static char printf_buffer[256];
        char *chr;
        if (frame == 60 * 5)
        {
            sprintf(printf_buffer, "%03d: %p => %02x\t%d%s\n\r",
                    y, registers.display_list, *registers.display_list, row_line_count,
                    wait_vbl ? " w" : "");
            chr = printf_buffer;
            while (*chr)
            {
                std_out_write(*chr++);
            }
        }
    }
#endif

    if (wait_vbl && y != 0)
    {
        // DL is stopped and waiting for VBL
        // generate full-length border line
        (void)tmds_encode_border(tmdsbuf, registers.border_color, DISPLAY_WIDTH_PIXELS / 8);
        // and we're done
        return;
    }

    if (y == 0) // start of frame - reset flags and counters
    {
        wait_vbl = false;
        row_line_count = 0;
    }

    uint8_t dl_instr = *registers.display_list;
    uint8_t dl_row_lines = registers.row_height;

    // Used for tracking where to blit pixel data
    uint32_t *p = tmdsbuf;

    // Left border
    if (dl_instr & MODE_BIT && registers.border_columns)
        p = tmds_encode_border(p, registers.border_color, registers.border_columns);

    // DL mode
    switch (dl_instr & 0b00001111)
    {
        // ------- Instructions -------

    case 0x0: // INSTR0 - blank lines
        dl_row_lines = dl_instr >> 4;
        (void)tmds_encode_border(p, registers.border_color, DISPLAY_WIDTH_PIXELS / 8);
        goto skip_right_border;

    case 0x1: // INSTR1 - JMP
        // Load DL address
        // registers.display_list = read_memory(registers.display_base << 16 & registers.display_list)
        registers.display_list = hires_mode_dl; // FIXME: HARDCODED!
        row_line_count = 0;                     // will start new row

        if (dl_instr & DLI_BIT)
            wait_vbl = true;

        // .display_list is already pointing to next instruction
        return cgia_render(y, p); // process next DL instruction

    case 0x2:                     // Load Memory
        ++registers.display_list; // Move to next DL instruction
        if (dl_instr & 0b00010000)
        { // memory scan
            // TODO:
            // registers.memory_scan = read_memory(registers.display_base << 16 & registers.display_list)
            registers.memory_scan = bitmap_data; // FIXME: HARDCODED!
            registers.display_list += 2;
        }
        if (dl_instr & 0b00100000)
        { // color scan
            // TODO:
            // registers.colour_scan = read_memory(registers.display_base << 16 & registers.display_list)
            registers.colour_scan = colour_data; // FIXME: HARDCODED!
            registers.display_list += 2;
        }
        if (dl_instr & 0b01000000)
        { // background scan
            // TODO:
            // registers.backgr_scan = read_memory(registers.display_base << 16 & registers.display_list)
            registers.backgr_scan = background_data; // FIXME: HARDCODED!
            registers.display_list += 2;
        }
        return cgia_render(y, p); // process next DL instruction

        // ------- Mode Rows -------

    case (0x3 | MODE_BIT): // MODE3 - bitmap mode
    {
        const int row_px = FRAME_WIDTH - (registers.border_columns << 5);
        const uint8_t row_height = registers.row_height + 1;
        interp_set_base(interp0, 0, row_height);
        interp_set_accumulator(interp0, 0, (uintptr_t)registers.memory_scan - row_height);
        interp_set_accumulator(interp1, 0, (uintptr_t)registers.colour_scan - 1);
        interp_set_accumulator(interp1, 1, (uintptr_t)registers.backgr_scan - 1);
        if (registers.transparent_background)
        {
            p = tmds_encode_mode_3_shared(p, row_px, &registers.background_color);
        }
        else
        {
            p = tmds_encode_mode_3_mapped(p, row_px);
        }

        // next raster line starts with next byte, but color/bg scan stays the same
        ++registers.memory_scan;
    }
    break;

        // ------- UNKNOWN MODE - generate pink line (should not happen)
    default:
        (void)tmds_encode_border(tmdsbuf, 115, DISPLAY_WIDTH_PIXELS / 8);
        dl_row_lines = row_line_count; // force moving to next DL instruction
        goto skip_right_border;
    }

    // Right border
    if (dl_instr & MODE_BIT && registers.border_columns)
        p = tmds_encode_border(p, registers.border_color, registers.border_columns);

skip_right_border:

    // Should we run a new DL row?
    if (row_line_count == dl_row_lines)
    {
        // Update scan pointers
        if (dl_instr & MODE_BIT)
        {
            // update scan pointers to current value
            registers.memory_scan = (uint8_t *)(uintptr_t)interp_peek_lane_result(interp0, 0) - row_line_count;
            registers.colour_scan = (uint8_t *)(uintptr_t)interp_peek_lane_result(interp1, 0);
            registers.backgr_scan = (uint8_t *)(uintptr_t)interp_peek_lane_result(interp1, 1);
        }

        // Reset line counter
        row_line_count = 0;

        // Move to next DL instruction
        ++registers.display_list;
    }
    else
    {
        // Move to next line of current DL row
        ++row_line_count;
    }
}

void cgia_task(void)
{
}
