#include "hardware/interp.h"

#include "sys/std.h"
#include <stdint.h>
#include <stdio.h>

#include "cgia.h"
#include "cgia_palette.h"
#include "tmds_encode_cgia.h"

#include "carrion-One_Zak_And_His_Kracken.h"
#include "display_lists.h"
#include "font_8.h"

#include "sys/out.h"

static const uint32_t __scratch_x("tmds_table") tmds_table[] = {
#include "tmds_table.h"
};

#define PALETTE_WORDS 4
uint32_t __attribute__((aligned(4))) cgia_palette[CGIA_COLORS_NUM * PALETTE_WORDS];

#define DISPLAY_WIDTH_PIXELS (FRAME_WIDTH / 2)
#define MAX_BORDER_COLUMNS   (FRAME_WIDTH / 2 / 8 / 2)

#define FRAME_CHARS (DISPLAY_WIDTH_PIXELS / 8)
uint32_t __attribute__((aligned(4))) scanline_buffer[FRAME_CHARS * 3];
uint8_t __attribute__((aligned(4))) screen[FRAME_CHARS * 30];
uint8_t __attribute__((aligned(4))) colour[FRAME_CHARS * 30];
uint8_t __attribute__((aligned(4))) backgr[FRAME_CHARS * 30];

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

static struct registers_t
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

    uint8_t *character_generator;

    uint8_t border_color;
    uint8_t row_height;
    uint8_t border_columns;
    bool transparent_background;
} __attribute__((aligned(4))) registers;

uint8_t __attribute__((aligned(4))) shared_color[2];

void cgia_init(void)
{
    init_palette();

    // FIXME: these should be initialized by CPU Operating System
    registers.border_color = 145;
    shared_color[0] = background_color_1;
    shared_color[1] = background_color_2;
    registers.row_height = 7;
    registers.display_list = hires_mode_dl;
    registers.memory_scan = screen;
    registers.colour_scan = colour;
    registers.backgr_scan = backgr;
    registers.character_generator = font8_data;

    // Config
    registers.transparent_background = false;
    registers.border_columns = 4;

    // TODO: fill with 0s
    for (int i = 0; i < FRAME_CHARS * 30; ++i)
    {
        screen[i] = 0; // i & 0xff;
    }
    screen[0] = 'R';
    screen[1] = 'E';
    screen[2] = 'A';
    screen[3] = 'D';
    screen[4] = 'Y';
    for (int i = 0; i < FRAME_CHARS * 30; ++i)
    {
        colour[i] = 150; // i % CGIA_COLORS_NUM;
        backgr[i] = 145; // ((CGIA_COLORS_NUM - 1) - i) % CGIA_COLORS_NUM;
    }
    backgr[FRAME_CHARS - 2 * registers.border_columns] = 150;
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

static inline uint8_t log_2(uint8_t x)
{
    if (registers.row_height == 0)
        return 0; // 1 => *1
    else if (registers.row_height < 2)
        return 1; // 2 => *2
    else if (registers.row_height < 4)
        return 2; // 3-4 => *4
    else if (registers.row_height < 8)
        return 3; // 5-8 => *8
    else if (registers.row_height < 16)
        return 4; // 9-16 => *16
    else if (registers.row_height < 32)
        return 5; // 17-32 => *32
    else if (registers.row_height < 64)
        return 6; // 33-64 => *64
    else if (registers.row_height < 128)
        return 7; // 65-128 => *128
    else
        return 8; // 129-255 => *256
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

    uint border_columns = registers.border_columns;
    if (border_columns > MAX_BORDER_COLUMNS)
        border_columns = MAX_BORDER_COLUMNS;
    uint row_columns = FRAME_CHARS - 2 * border_columns;

    // Left border
    if (dl_instr & MODE_BIT && border_columns)
        p = tmds_encode_border(p, registers.border_color, border_columns);

    // DL mode
    switch (dl_instr & 0b00001111)
    {
        // ------- Instructions --------------

    case 0x0: // INSTR0 - blank lines
        dl_row_lines = dl_instr >> 4;
        (void)tmds_encode_border(p, registers.border_color, DISPLAY_WIDTH_PIXELS / 8);
        goto skip_right_border;

    case 0x1: // INSTR1 - duplicate lines
        dl_row_lines = dl_instr >> 4;
        // FIXME: for now leave TMDS buffer as is - will display it again
        // TODO: store last TMDS buffer pointer at the end of the frame and copy to current one
        goto skip_right_border;

    case 0x2: // INSTR1 - JMP
        // Load DL address
        // registers.display_list = read_memory(registers.display_base << 16 & registers.display_list)
        registers.display_list = hires_mode_dl; // FIXME: HARDCODED!
        row_line_count = 0;                     // will start new row

        if (dl_instr & DLI_BIT)
            wait_vbl = true;

        // .display_list is already pointing to next instruction
        return cgia_render(y, p); // process next DL instruction

    case 0x3:                     // Load Memory
        ++registers.display_list; // Move to next DL instruction
        if (dl_instr & 0b00010000)
        { // memory scan
            // TODO:
            // registers.memory_scan = read_memory(registers.display_base << 16 & registers.display_list)
            registers.memory_scan = screen; // FIXME: HARDCODED!
            registers.display_list += 2;
        }
        if (dl_instr & 0b00100000)
        { // color scan
            // TODO:
            // registers.colour_scan = read_memory(registers.display_base << 16 & registers.display_list)
            registers.colour_scan = colour; // FIXME: HARDCODED!
            registers.display_list += 2;
        }
        if (dl_instr & 0b01000000)
        { // background scan
            // TODO:
            // registers.backgr_scan = read_memory(registers.display_base << 16 & registers.display_list)
            registers.backgr_scan = backgr; // FIXME: HARDCODED!
            registers.display_list += 2;
        }
        return cgia_render(y, p); // process next DL instruction

        // ------- Mode Rows --------------

    case (0x2 | MODE_BIT): // MODE2 - text/tile mode
    {
        interp_set_base(interp0, 0, 1);
        interp_set_accumulator(interp0, 0, (uintptr_t)registers.memory_scan - 1);
        interp_set_accumulator(interp1, 0, (uintptr_t)registers.colour_scan - 1);
        interp_set_accumulator(interp1, 1, (uintptr_t)registers.backgr_scan - 1);
        if (row_columns)
        {
            uint8_t char_shift = log_2(registers.row_height);
            load_textmode_buffer(scanline_buffer, row_columns, registers.character_generator + row_line_count, char_shift);
            p = tmds_encode_mode_3_mapped(p, scanline_buffer, row_columns);
        }
    }
    break;

    case (0x3 | MODE_BIT): // MODE3 - bitmap mode
    {
        const uint8_t row_height = registers.row_height + 1;
        interp_set_base(interp0, 0, row_height);
        interp_set_accumulator(interp0, 0, (uintptr_t)registers.memory_scan - row_height);
        interp_set_accumulator(interp1, 0, (uintptr_t)registers.colour_scan - 1);
        interp_set_accumulator(interp1, 1, (uintptr_t)registers.backgr_scan - 1);
        if (row_columns)
        {
            if (registers.transparent_background)
            {
                load_scanline_buffer_shared(scanline_buffer, row_columns);
                p = tmds_encode_mode_3_shared(p, scanline_buffer, row_columns);
            }
            else
            {
                load_scanline_buffer_mapped(scanline_buffer, row_columns);
                p = tmds_encode_mode_3_mapped(p, scanline_buffer, row_columns);
            }
        }

        // next raster line starts with next byte, but color/bg scan stay the same
        ++registers.memory_scan;
    }
    break;

    case (0x4 | MODE_BIT): // MODE4 - multicolor text/tile mode
    {
        interp_set_base(interp0, 0, 1);
        interp_set_accumulator(interp0, 0, (uintptr_t)registers.memory_scan - 1);
        interp_set_accumulator(interp1, 0, (uintptr_t)registers.colour_scan - 1);
        interp_set_accumulator(interp1, 1, (uintptr_t)registers.backgr_scan - 1);
        if (row_columns)
        {
            row_columns <<= 1; // this mode generates 4x8 cells, so requires 2x columns
            uint8_t char_shift = log_2(registers.row_height);
            load_textmode_buffer(scanline_buffer, row_columns, registers.character_generator + row_line_count, char_shift);
            p = tmds_encode_mode_5(p, scanline_buffer, row_columns);
        }
    }
    break;

    case (0x5 | MODE_BIT): // MODE5 - multicolor bitmap mode
    {
        const uint8_t row_height = registers.row_height + 1;
        interp_set_base(interp0, 0, row_height);
        interp_set_accumulator(interp0, 0, (uintptr_t)registers.memory_scan - row_height);
        interp_set_accumulator(interp1, 0, (uintptr_t)registers.colour_scan - 1);
        interp_set_accumulator(interp1, 1, (uintptr_t)registers.backgr_scan - 1);
        if (row_columns)
        {
            row_columns <<= 1; // this mode generates 4x8 cells, so requires 2x columns
            load_scanline_buffer_mapped(scanline_buffer, row_columns);
            p = tmds_encode_mode_5(p, scanline_buffer, row_columns);
        }

        // next raster line starts with next byte, but color/bg scan stay the same
        ++registers.memory_scan;
    }
    break;

    case (0x6 | MODE_BIT): // MODE4 - doubled multicolor text/tile mode
    {
        interp_set_base(interp0, 0, 1);
        interp_set_accumulator(interp0, 0, (uintptr_t)registers.memory_scan - 1);
        interp_set_accumulator(interp1, 0, (uintptr_t)registers.colour_scan - 1);
        interp_set_accumulator(interp1, 1, (uintptr_t)registers.backgr_scan - 1);
        if (row_columns)
        {
            uint8_t char_shift = log_2(registers.row_height);
            load_textmode_buffer(scanline_buffer, row_columns, registers.character_generator + row_line_count, char_shift);
            p = tmds_encode_mode_7(p, scanline_buffer, row_columns);
        }
    }
    break;

    case (0x7 | MODE_BIT): // MODE7 - doubled multicolor bitmap mode
    {
        const uint8_t row_height = registers.row_height + 1;
        interp_set_base(interp0, 0, row_height);
        interp_set_accumulator(interp0, 0, (uintptr_t)registers.memory_scan - row_height);
        interp_set_accumulator(interp1, 0, (uintptr_t)registers.colour_scan - 1);
        interp_set_accumulator(interp1, 1, (uintptr_t)registers.backgr_scan - 1);
        if (row_columns)
        {
            load_scanline_buffer_mapped(scanline_buffer, row_columns);
            p = tmds_encode_mode_7(p, scanline_buffer, row_columns);
        }

        // next raster line starts with next byte, but color/bg scan stay the same
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
    if (dl_instr & MODE_BIT && border_columns)
        p = tmds_encode_border(p, registers.border_color, border_columns);

skip_right_border:

    // Should we run a new DL row?
    if (row_line_count == dl_row_lines)
    {
        // Update scan pointers
        if (dl_instr & MODE_BIT)
        {
            // update scan pointers to current value
            registers.memory_scan = (uint8_t *)(uintptr_t)interp_peek_lane_result(interp0, 0);
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
