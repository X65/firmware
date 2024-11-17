#include "hardware/interp.h"

#include "cgia.h"
#include "cgia_encode.h"
#include "cgia_palette.h"

#include "example_data.h"
#include "font_8.h"
#include "images/carrion-One_Zak_And_His_Kracken.h"
#include "images/veto-the_mill.h"

#include "sys/out.h"

#define DISPLAY_WIDTH_PIXELS (FRAME_WIDTH / 2)
#define MAX_BORDER_COLUMNS   (DISPLAY_WIDTH_PIXELS / CGIA_COLUMN_PX / 2)

#define FRAME_CHARS (DISPLAY_WIDTH_PIXELS / CGIA_COLUMN_PX)

// --- Globals ---
struct cgia_t __attribute__((aligned(4))) CGIA
    = {0};

static struct cgia_plane_internal
{
    uint8_t *memory_scan;
    uint8_t *colour_scan;
    uint8_t *backgr_scan;
    uint8_t *char_gen;
}
__attribute__((aligned(4))) plane[CGIA_PLANES]
    = {0};

static uint8_t __attribute__((aligned(4))) sprite_line_data[SPRITE_MAX_WIDTH];

#define EXAMPLE_SPRITE_COUNT 8
static struct cgia_sprite_t __attribute__((aligned(4))) sprites[EXAMPLE_SPRITE_COUNT]
    = {0};

// --- temporary WIP ---
uint32_t __attribute__((aligned(4))) scanline_buffer[FRAME_CHARS * 3];
uint8_t __attribute__((aligned(4))) screen[FRAME_CHARS * 30];
uint8_t __attribute__((aligned(4))) colour[FRAME_CHARS * 30];
uint8_t __attribute__((aligned(4))) bckgnd[FRAME_CHARS * 30];

/* TEXT MODE */
#define EXAMPLE_DISPLAY_LIST text_mode_dl
#define EXAMPLE_BORDER_COLOR 145
#define EXAMPLE_ROW_HEIGHT   7
#define EXAMPLE_MEMORY_SCAN  screen
#define EXAMPLE_COLOUR_SCAN  colour
#define EXAMPLE_BACKGR_SCAN  bckgnd

// /* HiRes mode */
// #define EXAMPLE_DISPLAY_LIST hires_mode_dl
// #define EXAMPLE_BORDER_COLOR 3
// #define EXAMPLE_ROW_HEIGHT   7
// #define EXAMPLE_MEMORY_SCAN  hr_bitmap_data
// #define EXAMPLE_COLOUR_SCAN  hr_colour_data
// #define EXAMPLE_BACKGR_SCAN  hr_background_data

// /* MultiColor mode */
// #define EXAMPLE_DISPLAY_LIST multi_mode_dl
// #define EXAMPLE_BORDER_COLOR 0
// #define EXAMPLE_ROW_HEIGHT   0
// #define EXAMPLE_MEMORY_SCAN  mt_bitmap_data
// #define EXAMPLE_COLOUR_SCAN  mt_colour_data
// #define EXAMPLE_BACKGR_SCAN  mt_background_data

// ---
#define EXAMPLE_BG_COLOR_1 mt_background_color_1
#define EXAMPLE_BG_COLOR_2 mt_background_color_2

#define EXAMPLE_CHARGEN font8_data

// TODO: set when writing CGIA.plane[1].regs.sprite.active
static bool sprites_need_update = false;

void cgia_init(void)
{
    CGIA.back_color = EXAMPLE_BORDER_COLOR;
    // FIXME: these should be initialized by CPU Operating System
    CGIA.back_color = EXAMPLE_BORDER_COLOR;
    CGIA.plane[0].regs.bckgnd.shared_color[0] = EXAMPLE_BG_COLOR_1;
    CGIA.plane[0].regs.bckgnd.shared_color[1] = EXAMPLE_BG_COLOR_2;
    CGIA.plane[0].regs.bckgnd.row_height = EXAMPLE_ROW_HEIGHT;
    CGIA.plane[0].regs.bckgnd.border_columns = 4;
    plane[0].memory_scan = EXAMPLE_MEMORY_SCAN;
    plane[0].colour_scan = EXAMPLE_COLOUR_SCAN;
    plane[0].backgr_scan = EXAMPLE_BACKGR_SCAN;
    plane[0].char_gen = EXAMPLE_CHARGEN;

    CGIA.plane[1].regs.sprite.count = EXAMPLE_SPRITE_COUNT;
    for (uint8_t i = 0; i < EXAMPLE_SPRITE_COUNT; ++i)
    {
        sprites[i].flags = SPRITE_MASK_ACTIVE;
        sprites[i].flags |= SPRITE_MASK_MULTICOLOR;
        sprites[i].flags |= EXAMPLE_SPRITE_WIDTH - 1; // width
        sprites[i].lines_y = EXAMPLE_SPRITE_HEIGHT;
        sprites[i].color[0] = EXAMPLE_SPRITE_COLOR_1;
        sprites[i].color[1] = EXAMPLE_SPRITE_COLOR_2;
        sprites[i].color[2] = EXAMPLE_SPRITE_COLOR_3;
        sprites[i].pos_x = 54 * i - 5;
        sprites[i].pos_y = 8;
    }
    sprites[1].pos_y = -10;
    sprites[2].flags &= ~SPRITE_MASK_MULTICOLOR;
    sprites[2].flags |= SPRITE_MASK_MIRROR_X;
    sprites[2].flags |= SPRITE_MASK_MIRROR_Y;
    // sprites[2].flags |= SPRITE_MASK_DOUBLE_WIDTH;
    sprites[3].flags |= SPRITE_MASK_DOUBLE_WIDTH;
    sprites[4].flags |= SPRITE_MASK_MIRROR_X;
    sprites[5].flags |= SPRITE_MASK_MIRROR_Y;
    sprites[6].flags &= ~SPRITE_MASK_WIDTH;
    sprites[6].flags |= 2;

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
        colour[i] = 150;             // i % CGIA_COLORS_NUM;
        bckgnd[i] = CGIA.back_color; // ((CGIA_COLORS_NUM - 1) - i) % CGIA_COLORS_NUM;
    }
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
    if (x == 0)
        return 0; // 1 => *1
    else if (x < 2)
        return 1; // 2 => *2
    else if (x < 4)
        return 2; // 3-4 => *4
    else if (x < 8)
        return 3; // 5-8 => *8
    else if (x < 16)
        return 4; // 9-16 => *16
    else if (x < 32)
        return 5; // 17-32 => *32
    else if (x < 64)
        return 6; // 33-64 => *64
    else if (x < 128)
        return 7; // 65-128 => *128
    else
        return 8; // 129-255 => *256
}

#define MODE_BIT 0b00001000
#define DLI_BIT  0b10000000

void __not_in_flash_func(cgia_render)(uint y, uint32_t *rgbbuf, uint8_t recursion_depth)
{
    static uint8_t row_line_count = 0;
    static bool wait_vbl = true;

    // bail if sequence of commands is too long
    if (++recursion_depth > 8)
        return;

    if (wait_vbl && y != 0)
    {
        // DL is stopped and waiting for VBL
        // generate full-length border line
        (void)cgia_encode_border(rgbbuf, DISPLAY_WIDTH_PIXELS / CGIA_COLUMN_PX, CGIA.back_color);
        // and we're done
        goto sprites;
    }

    if (y == 0) // start of frame - reset flags and counters
    {
        wait_vbl = false;
        row_line_count = 0;
    }

    // --- BACKGROUND ---
    {
        uint8_t *bckgnd_bank = EXAMPLE_DISPLAY_LIST; // psram + (CGIA.bckgnd_bank << 16)
        uint8_t dl_row_lines = CGIA.plane[0].regs.bckgnd.row_height;

        // Used for tracking where to blit pixel data
        uint32_t *p = rgbbuf;

        uint border_columns = CGIA.plane[0].regs.bckgnd.border_columns;
        if (border_columns > MAX_BORDER_COLUMNS)
            border_columns = MAX_BORDER_COLUMNS;
        uint row_columns = FRAME_CHARS - 2 * border_columns;

        uint8_t dl_instr = bckgnd_bank[CGIA.plane[0].offset];

#if 0
        { // DL debugger
            static int frame = 0;
            if (y == 0)
                ++frame;
            if (frame == 60 * 5)
            {
                printf("%03d: %d => %02x\t%d%s\n\r",
                       y, CGIA.plane[0].offset, dl_instr, row_line_count,
                       wait_vbl ? " w" : "");
            }
        }
#endif
        // Left border
        if (dl_instr & MODE_BIT && border_columns)
            p = cgia_encode_border(p, border_columns, CGIA.back_color);

        // DL mode
        switch (dl_instr & 0b00001111)
        {
            // ------- Instructions --------------

        case 0x0: // INSTR0 - blank lines
            dl_row_lines = dl_instr >> 4;
            (void)cgia_encode_border(p, DISPLAY_WIDTH_PIXELS / CGIA_COLUMN_PX, CGIA.back_color);
            goto skip_right_border;

        case 0x1: // INSTR1 - duplicate lines
            dl_row_lines = dl_instr >> 4;
            // FIXME: for now leave RGB buffer as is - will display it again
            // TODO: store last RGB buffer pointer at the end of the frame and copy to current one
            goto skip_right_border;

        case 0x2: // INSTR1 - JMP
            // Load DL address
            CGIA.plane[0].offset = (uint16_t)((bckgnd_bank[++CGIA.plane[0].offset])
                                              | (bckgnd_bank[++CGIA.plane[0].offset] << 8));
            CGIA.plane[0].offset = 0; // FIXME: HARDCODED!
            row_line_count = 0;       // will start new row

            if (dl_instr & DLI_BIT)
                wait_vbl = true;

            // .display_list is already pointing to proper instruction
            return cgia_render(y, p, recursion_depth); // process next DL instruction

        case 0x3: // Load Memory

            ++CGIA.plane[0].offset; // Move to next DL byte
            if (dl_instr & 0b00010000)
            {
                plane[0].memory_scan = bckgnd_bank
                                       + (uint16_t)((bckgnd_bank[++CGIA.plane[0].offset])
                                                    | (bckgnd_bank[++CGIA.plane[0].offset] << 8));
                plane[0].memory_scan = EXAMPLE_MEMORY_SCAN; // FIXME: HARDCODED!
            }
            if (dl_instr & 0b00100000)
            {
                plane[0].colour_scan = bckgnd_bank
                                       + (uint16_t)((bckgnd_bank[++CGIA.plane[0].offset])
                                                    | (bckgnd_bank[++CGIA.plane[0].offset] << 8));
                plane[0].colour_scan = EXAMPLE_COLOUR_SCAN; // FIXME: HARDCODED!
            }
            if (dl_instr & 0b01000000)
            {
                plane[0].backgr_scan = bckgnd_bank
                                       + (uint16_t)((bckgnd_bank[++CGIA.plane[0].offset])
                                                    | (bckgnd_bank[++CGIA.plane[0].offset] << 8));
                plane[0].backgr_scan = EXAMPLE_BACKGR_SCAN; // FIXME: HARDCODED!
            }
            if (dl_instr & 0b10000000)
            {
                plane[0].char_gen = bckgnd_bank
                                    + (uint16_t)((bckgnd_bank[++CGIA.plane[0].offset])
                                                 | (bckgnd_bank[++CGIA.plane[0].offset] << 8));
                plane[0].char_gen = EXAMPLE_CHARGEN; // FIXME: HARDCODED!
            }
            return cgia_render(y, p, recursion_depth); // process next DL instruction

            // ------- Mode Rows --------------

        case (0x2 | MODE_BIT): // MODE2 (A) - text/tile mode
        {
            interp_set_base(interp0, 0, 1);
            interp_set_accumulator(interp0, 0, (uintptr_t)plane[0].memory_scan - 1);
            interp_set_accumulator(interp1, 0, (uintptr_t)plane[0].colour_scan - 1);
            interp_set_accumulator(interp1, 1, (uintptr_t)plane[0].backgr_scan - 1);
            if (row_columns)
            {
                uint8_t char_shift = log_2(CGIA.plane[0].regs.bckgnd.row_height);
                load_textmode_buffer(scanline_buffer, row_columns, plane[0].char_gen + row_line_count, char_shift);
                p = cgia_encode_mode_3_mapped(p, scanline_buffer, row_columns);
            }
        }
        break;

        case (0x3 | MODE_BIT): // MODE3 (B) - bitmap mode
        {
            const uint8_t row_height = CGIA.plane[0].regs.bckgnd.row_height + 1;
            interp_set_base(interp0, 0, row_height);
            interp_set_accumulator(interp0, 0, (uintptr_t)plane[0].memory_scan - row_height);
            interp_set_accumulator(interp1, 0, (uintptr_t)plane[0].colour_scan - 1);
            interp_set_accumulator(interp1, 1, (uintptr_t)plane[0].backgr_scan - 1);
            if (row_columns)
            {
                if (false) // FIXME: fill background using DMA and always use transparency code
                {
                    load_scanline_buffer_shared(scanline_buffer, row_columns);
                    p = cgia_encode_mode_3_shared(p, scanline_buffer, row_columns);
                }
                else
                {
                    load_scanline_buffer_mapped(scanline_buffer, row_columns);
                    p = cgia_encode_mode_3_mapped(p, scanline_buffer, row_columns);
                }
            }

            // next raster line starts with next byte, but color/bg scan stay the same
            ++plane[0].memory_scan;
        }
        break;

        case (0x4 | MODE_BIT): // MODE4 (C) - multicolor text/tile mode
        {
            interp_set_base(interp0, 0, 1);
            interp_set_accumulator(interp0, 0, (uintptr_t)plane[0].memory_scan - 1);
            interp_set_accumulator(interp1, 0, (uintptr_t)plane[0].colour_scan - 1);
            interp_set_accumulator(interp1, 1, (uintptr_t)plane[0].backgr_scan - 1);
            if (row_columns)
            {
                row_columns <<= 1; // this mode generates 4x8 cells, so requires 2x columns
                uint8_t char_shift = log_2(CGIA.plane[0].regs.bckgnd.row_height);
                load_textmode_buffer(scanline_buffer, row_columns, plane[0].char_gen + row_line_count, char_shift);
                p = cgia_encode_mode_5(p, scanline_buffer, row_columns, CGIA.plane[0].regs.bckgnd.shared_color);
            }
        }
        break;

        case (0x5 | MODE_BIT): // MODE5 (D) - multicolor bitmap mode
        {
            const uint8_t row_height = CGIA.plane[0].regs.bckgnd.row_height + 1;
            interp_set_base(interp0, 0, row_height);
            interp_set_accumulator(interp0, 0, (uintptr_t)plane[0].memory_scan - row_height);
            interp_set_accumulator(interp1, 0, (uintptr_t)plane[0].colour_scan - 1);
            interp_set_accumulator(interp1, 1, (uintptr_t)plane[0].backgr_scan - 1);
            if (row_columns)
            {
                row_columns <<= 1; // this mode generates 4x8 cells, so requires 2x columns
                load_scanline_buffer_mapped(scanline_buffer, row_columns);
                p = cgia_encode_mode_5(p, scanline_buffer, row_columns, CGIA.plane[0].regs.bckgnd.shared_color);
            }

            // next raster line starts with next byte, but color/bg scan stay the same
            ++plane[0].memory_scan;
        }
        break;

        case (0x6 | MODE_BIT): // MODE6 (E) - doubled multicolor text/tile mode
        {
            interp_set_base(interp0, 0, 1);
            interp_set_accumulator(interp0, 0, (uintptr_t)plane[0].memory_scan - 1);
            interp_set_accumulator(interp1, 0, (uintptr_t)plane[0].colour_scan - 1);
            interp_set_accumulator(interp1, 1, (uintptr_t)plane[0].backgr_scan - 1);
            if (row_columns)
            {
                uint8_t char_shift = log_2(CGIA.plane[0].regs.bckgnd.row_height);
                load_textmode_buffer(scanline_buffer, row_columns, plane[0].char_gen + row_line_count, char_shift);
                p = cgia_encode_mode_7(p, scanline_buffer, row_columns, CGIA.plane[0].regs.bckgnd.shared_color);
            }
        }
        break;

        case (0x7 | MODE_BIT): // MODE7 (F) - doubled multicolor bitmap mode
        {
            const uint8_t row_height = CGIA.plane[0].regs.bckgnd.row_height + 1;
            interp_set_base(interp0, 0, row_height);
            interp_set_accumulator(interp0, 0, (uintptr_t)plane[0].memory_scan - row_height);
            interp_set_accumulator(interp1, 0, (uintptr_t)plane[0].colour_scan - 1);
            interp_set_accumulator(interp1, 1, (uintptr_t)plane[0].backgr_scan - 1);
            if (row_columns)
            {
                load_scanline_buffer_mapped(scanline_buffer, row_columns);
                p = cgia_encode_mode_7(p, scanline_buffer, row_columns, CGIA.plane[0].regs.bckgnd.shared_color);
            }

            // next raster line starts with next byte, but color/bg scan stay the same
            ++plane[0].memory_scan;
        }
        break;

        // ------- UNKNOWN MODE - generate pink line (should not happen)
        default:
            (void)cgia_encode_border(rgbbuf, DISPLAY_WIDTH_PIXELS / CGIA_COLUMN_PX, 234);
            dl_row_lines = row_line_count; // force moving to next DL instruction
            goto skip_right_border;
        }

        // Right border
        if (dl_instr & MODE_BIT && border_columns)
            p = cgia_encode_border(p, border_columns, CGIA.back_color);

    skip_right_border:
        // Should we run a new DL row?
        if (row_line_count == dl_row_lines)
        {
            // Update scan pointers
            if (dl_instr & MODE_BIT)
            {
                // update scan pointers to next value
                plane[0].memory_scan = (uint8_t *)(uintptr_t)interp_get_accumulator(interp0, 0) + 1;
                plane[0].colour_scan = (uint8_t *)(uintptr_t)interp_get_accumulator(interp1, 0) + 1;
                plane[0].backgr_scan = (uint8_t *)(uintptr_t)interp_get_accumulator(interp1, 1) + 1;
            }

            // Reset line counter
            row_line_count = 0;

            // Move to next DL instruction
            ++CGIA.plane[0].offset;
        }
        else
        {
            // Move to next line of current DL row
            ++row_line_count;
        }
    }

sprites:
    // --- SPRITES ---
    {
        size_t count = CGIA.plane[1].regs.sprite.count;
        struct cgia_sprite_t *sprite = sprites + count;
        while (count--)
        {
            // render sprites in reverse order
            // so lower indexed sprites have higher visual priority
            --sprite;
            if (sprite->flags & SPRITE_MASK_ACTIVE)
            {
                int sprite_line = (sprite->flags & SPRITE_MASK_MIRROR_Y)
                                      ? sprite->pos_y + sprite->lines_y - 1 - y
                                      : y - sprite->pos_y;
                if (sprite_line >= 0 && sprite_line < sprite->lines_y)
                {
                    const uint8_t sprite_width = sprite->flags & SPRITE_MASK_WIDTH;
                    uint8_t line_bytes = sprite_width + 1;
                    const uint sprite_offset = sprite_line * line_bytes;

                    uint8_t *dst = sprite_line_data;
                    uint8_t *src;
                    if (sprite->flags & SPRITE_MASK_MIRROR_X)
                    {
                        src = example_sprite_data + sprite_offset + sprite_width;
                        do
                        {
                            *dst++ = *src--;
                        } while (--line_bytes);
                    }
                    else
                    {
                        src = example_sprite_data + sprite_offset;
                        do
                        {
                            *dst++ = *src++;
                        } while (--line_bytes);
                    }

                    cgia_encode_sprite(rgbbuf, (uint32_t *)sprite, sprite_line_data, sprite_width);
                }
            }
        }
    }

    if (sprites_need_update)
    {
        sprites_need_update = false;
        for (uint8_t s = 0; s < CGIA.plane[1].regs.sprite.count; ++s)
        {
            // TODO: copy memory to sprites[EXAMPLE_SPRITE_COUNT]
            // NOTE: use DMA controlled blocks transfer
        }
    }
}

#define SPRITE_PADDING (SPRITE_MAX_WIDTH * CGIA_COLUMN_PX)
#define MIN_SPRITE_X   (-SPRITE_PADDING)
#define MIN_SPRITE_Y   (-EXAMPLE_SPRITE_HEIGHT)

void cgia_vbl(void)
{
    // TODO: trigger CPU NMI

    // REMOVEME: example VBL service routine
    sprites[0].pos_x += 1;
    sprites[0].pos_y += 1;
    sprites[1].pos_x -= 1;
    sprites[1].pos_y += 1;
    sprites[2].pos_x += 1;
    sprites[2].pos_y -= 1;
    sprites[3].pos_x -= 1;
    sprites[3].pos_y -= 1;
    sprites[4].pos_x += 1;
    sprites[4].pos_y += 2;
    sprites[5].pos_x -= 2;
    sprites[5].pos_y += 1;
    sprites[6].pos_x += 2;
    sprites[6].pos_y -= 1;
    sprites[7].pos_x -= 1;
    sprites[7].pos_y -= 2;
    for (uint8_t i = 0; i < EXAMPLE_SPRITE_COUNT; ++i)
    {
        if (sprites[i].pos_x > DISPLAY_WIDTH_PIXELS)
            sprites[i].pos_x = MIN_SPRITE_X;
        if (sprites[i].pos_x < MIN_SPRITE_X)
            sprites[i].pos_x = DISPLAY_WIDTH_PIXELS;
        if (sprites[i].pos_y > FRAME_HEIGHT)
            sprites[i].pos_y = MIN_SPRITE_Y;
        if (sprites[i].pos_y < MIN_SPRITE_Y)
            sprites[i].pos_y = FRAME_HEIGHT;
    }

    static int frame = 0;
    ++frame;
    if (frame % 60 == 0)
    {
        // blink cursor
        uint8_t bg = bckgnd[FRAME_CHARS - 2 * CGIA.plane[0].regs.bckgnd.border_columns];
        bckgnd[FRAME_CHARS - 2 * CGIA.plane[0].regs.bckgnd.border_columns] = colour[FRAME_CHARS - 2 * CGIA.plane[0].regs.bckgnd.border_columns];
        colour[FRAME_CHARS - 2 * CGIA.plane[0].regs.bckgnd.border_columns] = bg;
    }
}
