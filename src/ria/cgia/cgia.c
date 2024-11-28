#include "hardware/dma.h"
#include "hardware/interp.h"

#include "cgia.h"
#include "cgia_encode.h"
#include "cgia_palette.h"

#include "images/sotb-1.h"

#include "sys/out.h"

#include <string.h>

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
    uint8_t row_line_count;
    bool wait_vbl;
    uint32_t __attribute__((aligned(4))) scanline_buffer[FRAME_CHARS * 3];
}
__attribute__((aligned(4))) plane_int[CGIA_PLANES]
    = {0};

static uint8_t __attribute__((aligned(4))) sprite_line_data[SPRITE_MAX_WIDTH];

// --- temporary WIP ---
uint8_t __attribute__((aligned(4))) video_bank[256 * 256];
uint8_t __attribute__((aligned(4))) spr_bank[256 * 256];

// ---
#define EXAMPLE_BORDER_COLOR 145
#define EXAMPLE_TEXT_COLOR   150

// TODO: set when writing CGIA.plane[1].regs.sprite.active
static bool sprites_need_update = false;

struct dma_control_block
{
    uint8_t *read;
    uint8_t *write;
    uint32_t len;
};

// DMA channels to copy blocks of data
// ctrl_chan loads control blocks into data_chan, which executes them.
int ctrl_chan;
int data_chan;

// DMA channel to fill raster line with background color
int back_chan;

void cgia_init(void)
{
    // All planes should initially wait for VBL
    for (uint i = 0; i < CGIA_PLANES; ++i)
    {
        plane_int[i].wait_vbl = true;
    }

    // DMA

    ctrl_chan = dma_claim_unused_channel(true);
    data_chan = dma_claim_unused_channel(true);

    dma_channel_config c = dma_channel_get_default_config(ctrl_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, 3); // 1 << 3 byte boundary on write ptr
    dma_channel_configure(
        ctrl_chan,
        &c,
        &dma_hw->ch[data_chan].al3_transfer_count, // Initial write address
        NULL,                                      // Initial read address
        2,                                         // Halt after each control block
        false                                      // Don't start yet
    );

    c = dma_channel_get_default_config(data_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_chain_to(&c, ctrl_chan);
    dma_channel_configure(
        data_chan,
        &c,
        NULL,
        NULL, // Initial read address and transfer count are unimportant;
        0,    // the control channel will reprogram them each time.
        false // Don't start yet.
    );

    back_chan = dma_claim_unused_channel(true);
    c = dma_channel_get_default_config(back_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);

    dma_channel_configure(
        back_chan,
        &c,
        NULL,
        NULL,
        DISPLAY_WIDTH_PIXELS,
        false);

    // FIXME: these should be initialized by CPU Operating System
    CGIA.back_color = EXAMPLE_BORDER_COLOR;

    uint8_t p;

    p = 0;
    CGIA.planes |= (0x01 << p);
    CGIA.plane[p].regs.bckgnd.flags = PLANE_MASK_TRANSPARENT;
    CGIA.plane[p].regs.bckgnd.shared_color[0] = 0;
    CGIA.plane[p].regs.bckgnd.shared_color[1] = 0;
    CGIA.plane[p].regs.bckgnd.row_height = 7;
    CGIA.plane[p].regs.bckgnd.border_columns = 4;
    CGIA.plane[p].regs.bckgnd.scroll = 0;
    memcpy(video_bank + video_offset, bitmap_data, sizeof(bitmap_data));
    memcpy(video_bank + color_offset, color_data, sizeof(color_data));
    memcpy(video_bank + bkgnd_offset, bkgnd_data, sizeof(bkgnd_data));
    memcpy(video_bank + dl_offset, display_list, sizeof(display_list));
    CGIA.plane[p].offset = dl_offset;
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

uint32_t *fill_back(
    uint32_t *buf,
    uint32_t columns,
    uint32_t color_idx)
{
    dma_channel_wait_for_finish_blocking(back_chan);
    dma_channel_set_write_addr(back_chan, buf, false);
    dma_channel_set_read_addr(back_chan, &cgia_rgb_palette[color_idx], false);
    const uint pixels = columns * CGIA_COLUMN_PX;
    dma_channel_set_trans_count(back_chan, pixels, true);
    return buf + pixels;
}

void __not_in_flash_func(cgia_render)(uint y, uint32_t *rgbbuf, uint8_t recursion_depth)
{
    static struct cgia_plane_t *plane;
    static struct cgia_plane_internal *plane_data;

    // bail if sequence of commands is too long
    if (++recursion_depth > 8)
        return;

    // track whether we need to fill line with background color
    // for transparent or sprite planes
    bool line_background_filled = false;

    for (uint p = 0; p < CGIA_PLANES; ++p)
    {
        if (CGIA.planes & (0x10u << p))
        {
            /* --- SPRITES --- */
            if (!(CGIA.planes & (1u << p)))
                continue; // next if not enabled

            if (!line_background_filled)
            {
                (void)fill_back(rgbbuf, DISPLAY_WIDTH_PIXELS / CGIA_COLUMN_PX, CGIA.back_color);
                line_background_filled = true;
            }

            plane = CGIA.plane + p;

            uint8_t *sprite_bank = spr_bank; // psram + (CGIA.sprite_bank << 16)
            struct cgia_sprite_t *sprites = (struct cgia_sprite_t *)(sprite_bank + plane->offset);

            size_t count = plane->regs.sprite.count;
            struct cgia_sprite_t *sprite = sprites + count;

            // wait until back fill is done, as it may overwrite sprites on the right side
            dma_channel_wait_for_finish_blocking(back_chan);

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
                        uint8_t *src = sprite_bank + sprite->data_offset;
                        if (sprite->flags & SPRITE_MASK_MIRROR_X)
                        {
                            src += sprite_offset + sprite_width;
                            do
                            {
                                *dst++ = *src--;
                            } while (--line_bytes);
                        }
                        else
                        {
                            src += sprite_offset;
                            do
                            {
                                *dst++ = *src++;
                            } while (--line_bytes);
                        }

                        cgia_encode_sprite(rgbbuf, (uint32_t *)sprite, sprite_line_data, sprite_width);
                    }
                }
            }

            if (sprites_need_update)
            {
                sprites_need_update = false;
                for (uint8_t s = 0; s < plane->regs.sprite.count; ++s)
                {
                    // TODO: copy memory to sprites[EXAMPLE_SPRITE_COUNT]
                    // NOTE: use DMA controlled blocks transfer
                }
            }
        }
        else
        {
            /* --- BACKGROUND --- */
            plane = CGIA.plane + p;
            plane_data = plane_int + p;

            if (y == 0) // start of frame - reset flags and counters
            {
                plane_data->wait_vbl = false;
                plane_data->row_line_count = 0;
            }

            if (!(CGIA.planes & (1u << p)))
                continue; // next if not enabled

            if (plane_data->wait_vbl && y != 0)
            {
                // DL is stopped and waiting for VBL
                // generate full-length border line
                (void)fill_back(rgbbuf, DISPLAY_WIDTH_PIXELS / CGIA_COLUMN_PX, CGIA.back_color);
                // and we're done
                continue;
            }

            uint8_t *bckgnd_bank = video_bank; // psram + (CGIA.bckgnd_bank << 16)
            uint8_t dl_instr = bckgnd_bank[plane->offset];
            uint8_t instr_code = dl_instr & 0b00001111;

            uint8_t dl_row_lines = plane->regs.bckgnd.row_height;

            // first process instructions - they need less preparation and can be shortcutted
            if (!(dl_instr & MODE_BIT))
            {
                switch (instr_code)
                {
                    // ------- Instructions --------------

                case 0x0: // INSTR0 - blank lines
                    dl_row_lines = dl_instr >> 4;
                    // fill already running
                    goto plane_epilogue;

                case 0x1: // INSTR1 - duplicate lines
                    dl_row_lines = dl_instr >> 4;
                    // FIXME: for now leave RGB buffer as is - will display it again
                    // TODO: store last RGB buffer pointer at the end of the frame and copy to current one
                    goto plane_epilogue;

                case 0x2: // INSTR1 - JMP
                    // Load DL address
                    plane->offset = (uint16_t)((bckgnd_bank[++plane->offset])
                                               | (bckgnd_bank[++plane->offset] << 8));
                    plane_data->row_line_count = 0; // will start new row

                    if (dl_instr & DLI_BIT)
                        plane_data->wait_vbl = true;

                    // .display_list is already pointing to proper instruction
                    return cgia_render(y, rgbbuf, recursion_depth); // process next DL instruction

                case 0x3: // Load Memory
                    if (dl_instr & 0b00010000)
                    {
                        plane_data->memory_scan = bckgnd_bank
                                                  + (uint16_t)((bckgnd_bank[++plane->offset])
                                                               | (bckgnd_bank[++plane->offset] << 8));
                    }
                    if (dl_instr & 0b00100000)
                    {
                        plane_data->colour_scan = bckgnd_bank
                                                  + (uint16_t)((bckgnd_bank[++plane->offset])
                                                               | (bckgnd_bank[++plane->offset] << 8));
                    }
                    if (dl_instr & 0b01000000)
                    {
                        plane_data->backgr_scan = bckgnd_bank
                                                  + (uint16_t)((bckgnd_bank[++plane->offset])
                                                               | (bckgnd_bank[++plane->offset] << 8));
                    }
                    if (dl_instr & 0b10000000)
                    {
                        plane_data->char_gen = bckgnd_bank
                                               + (uint16_t)((bckgnd_bank[++plane->offset])
                                                            | (bckgnd_bank[++plane->offset] << 8));
                    }
                    ++plane->offset;                                // Move to next DL instruction
                    return cgia_render(y, rgbbuf, recursion_depth); // process next DL instruction

                case 0x4: // Set 8-bit register
                {
                    ((uint8_t *)&plane->regs)[(dl_instr & 0b01110000) >> 4] = bckgnd_bank[++plane->offset];
                }
                    ++plane->offset;                                // Move to next DL instruction
                    return cgia_render(y, rgbbuf, recursion_depth); // process next DL instruction

                // ------- UNKNOWN INSTRUCTION
                default:
                    (void)fill_back(rgbbuf, DISPLAY_WIDTH_PIXELS / CGIA_COLUMN_PX, 234);
                    dl_row_lines = plane_data->row_line_count; // force moving to next DL instruction
                    goto plane_epilogue;
                }
            }
            else
            {
                // If it is a blank line (INSTR0) or first plane is transparent
                // fill the whole line with background color
                if (instr_code == 0 || (!line_background_filled && plane->regs.bckgnd.flags & PLANE_MASK_TRANSPARENT))
                {
                    (void)fill_back(rgbbuf, DISPLAY_WIDTH_PIXELS / CGIA_COLUMN_PX, CGIA.back_color);
                }

                uint border_columns = plane->regs.bckgnd.border_columns;
                if (border_columns > MAX_BORDER_COLUMNS)
                    border_columns = MAX_BORDER_COLUMNS;
                uint row_columns = FRAME_CHARS - 2 * border_columns;

                // Used for tracking where to blit pixel data
                uint32_t *buf = rgbbuf + border_columns * CGIA_COLUMN_PX + plane->regs.bckgnd.scroll;

                // ------- Mode Rows --------------
                switch (instr_code)
                {

                case (0x2 | MODE_BIT): // MODE2 (A) - text/tile mode
                {
                    interp_set_base(interp0, 0, 1);
                    interp_set_accumulator(interp0, 0, (uintptr_t)plane_data->memory_scan - 1);
                    interp_set_accumulator(interp1, 0, (uintptr_t)plane_data->colour_scan - 1);
                    interp_set_accumulator(interp1, 1, (uintptr_t)plane_data->backgr_scan - 1);
                    if (row_columns)
                    {
                        uint8_t char_shift = log_2(plane->regs.bckgnd.row_height);
                        load_textmode_buffer(plane_data->scanline_buffer, row_columns, plane_data->char_gen + plane_data->row_line_count, char_shift);
                        buf = cgia_encode_mode_3_mapped(buf, plane_data->scanline_buffer, row_columns);
                    }
                }
                break;

                case (0x3 | MODE_BIT): // MODE3 (B) - bitmap mode
                {
                    const uint8_t row_height = plane->regs.bckgnd.row_height + 1;
                    interp_set_base(interp0, 0, row_height);
                    interp_set_accumulator(interp0, 0, (uintptr_t)plane_data->memory_scan - row_height);
                    interp_set_accumulator(interp1, 0, (uintptr_t)plane_data->colour_scan - 1);
                    interp_set_accumulator(interp1, 1, (uintptr_t)plane_data->backgr_scan - 1);
                    if (row_columns)
                    {
                        if (plane->regs.bckgnd.flags & PLANE_MASK_TRANSPARENT)
                        {
                            load_scanline_buffer_shared(plane_data->scanline_buffer, row_columns);
                            buf = cgia_encode_mode_3_shared(buf, plane_data->scanline_buffer, row_columns);
                        }
                        else
                        {
                            load_scanline_buffer_mapped(plane_data->scanline_buffer, row_columns);
                            buf = cgia_encode_mode_3_mapped(buf, plane_data->scanline_buffer, row_columns);
                        }
                    }

                    // next raster line starts with next byte, but color/bg scan stay the same
                    ++plane_data->memory_scan;
                }
                break;

                case (0x4 | MODE_BIT): // MODE4 (C) - multicolor text/tile mode
                {
                    interp_set_base(interp0, 0, 1);
                    interp_set_accumulator(interp0, 0, (uintptr_t)plane_data->memory_scan - 1);
                    interp_set_accumulator(interp1, 0, (uintptr_t)plane_data->colour_scan - 1);
                    interp_set_accumulator(interp1, 1, (uintptr_t)plane_data->backgr_scan - 1);
                    if (row_columns)
                    {
                        row_columns <<= 1; // this mode generates 4x8 cells, so requires 2x columns
                        uint8_t char_shift = log_2(plane->regs.bckgnd.row_height);
                        load_textmode_buffer(plane_data->scanline_buffer, row_columns, plane_data->char_gen + plane_data->row_line_count, char_shift);
                        if (plane->regs.bckgnd.flags & PLANE_MASK_TRANSPARENT)
                        {
                            buf = cgia_encode_mode_5_shared(buf, plane_data->scanline_buffer, row_columns, plane->regs.bckgnd.shared_color);
                        }
                        else
                        {
                            buf = cgia_encode_mode_5_mapped(buf, plane_data->scanline_buffer, row_columns, plane->regs.bckgnd.shared_color);
                        }
                    }
                }
                break;

                case (0x5 | MODE_BIT): // MODE5 (D) - multicolor bitmap mode
                {
                    const uint8_t row_height = plane->regs.bckgnd.row_height + 1;
                    interp_set_base(interp0, 0, row_height);
                    interp_set_accumulator(interp0, 0, (uintptr_t)plane_data->memory_scan - row_height);
                    interp_set_accumulator(interp1, 0, (uintptr_t)plane_data->colour_scan - 1);
                    interp_set_accumulator(interp1, 1, (uintptr_t)plane_data->backgr_scan - 1);
                    if (row_columns)
                    {
                        row_columns <<= 1; // this mode generates 4x8 cells, so requires 2x columns
                        load_scanline_buffer_mapped(plane_data->scanline_buffer, row_columns);
                        if (plane->regs.bckgnd.flags & PLANE_MASK_TRANSPARENT)
                        {
                            buf = cgia_encode_mode_5_shared(buf, plane_data->scanline_buffer, row_columns, plane->regs.bckgnd.shared_color);
                        }
                        else
                        {
                            buf = cgia_encode_mode_5_mapped(buf, plane_data->scanline_buffer, row_columns, plane->regs.bckgnd.shared_color);
                        }
                    }

                    // next raster line starts with next byte, but color/bg scan stay the same
                    ++plane_data->memory_scan;
                }
                break;

                case (0x6 | MODE_BIT): // MODE6 (E) - doubled multicolor text/tile mode
                {
                    interp_set_base(interp0, 0, 1);
                    interp_set_accumulator(interp0, 0, (uintptr_t)plane_data->memory_scan - 1);
                    interp_set_accumulator(interp1, 0, (uintptr_t)plane_data->colour_scan - 1);
                    interp_set_accumulator(interp1, 1, (uintptr_t)plane_data->backgr_scan - 1);
                    if (row_columns)
                    {
                        uint8_t char_shift = log_2(plane->regs.bckgnd.row_height);
                        load_textmode_buffer(plane_data->scanline_buffer, row_columns, plane_data->char_gen + plane_data->row_line_count, char_shift);
                        if (plane->regs.bckgnd.flags & PLANE_MASK_TRANSPARENT)
                        {
                            buf = cgia_encode_mode_7_shared(buf, plane_data->scanline_buffer, row_columns, plane->regs.bckgnd.shared_color);
                        }
                        else
                        {
                            buf = cgia_encode_mode_7_mapped(buf, plane_data->scanline_buffer, row_columns, plane->regs.bckgnd.shared_color);
                        }
                    }
                }
                break;

                case (0x7 | MODE_BIT): // MODE7 (F) - doubled multicolor bitmap mode
                {
                    const uint8_t row_height = plane->regs.bckgnd.row_height + 1;
                    interp_set_base(interp0, 0, row_height);
                    interp_set_accumulator(interp0, 0, (uintptr_t)plane_data->memory_scan - row_height);
                    interp_set_accumulator(interp1, 0, (uintptr_t)plane_data->colour_scan - 1);
                    interp_set_accumulator(interp1, 1, (uintptr_t)plane_data->backgr_scan - 1);
                    if (row_columns)
                    {
                        load_scanline_buffer_mapped(plane_data->scanline_buffer, row_columns);
                        if (plane->regs.bckgnd.flags & PLANE_MASK_TRANSPARENT)
                        {
                            buf = cgia_encode_mode_7_shared(buf, plane_data->scanline_buffer, row_columns, plane->regs.bckgnd.shared_color);
                        }
                        else
                        {
                            buf = cgia_encode_mode_7_mapped(buf, plane_data->scanline_buffer, row_columns, plane->regs.bckgnd.shared_color);
                        }
                    }

                    // next raster line starts with next byte, but color/bg scan stay the same
                    ++plane_data->memory_scan;
                }
                break;

                // ------- UNKNOWN MODE - generate pink line (should not happen)
                default:
                    (void)fill_back(rgbbuf, DISPLAY_WIDTH_PIXELS / CGIA_COLUMN_PX, 234);
                    dl_row_lines = plane_data->row_line_count; // force moving to next DL instruction
                    goto plane_epilogue;
                }

                // borders
                if (dl_instr & MODE_BIT && border_columns)
                {
                    buf = fill_back(rgbbuf, border_columns, CGIA.back_color);
                    buf += row_columns * CGIA_COLUMN_PX;
                    fill_back(buf, border_columns, CGIA.back_color);
                }
            }

        plane_epilogue:
            // this line has something to draw on
            line_background_filled = true;

            // Should we run a new DL row?
            if (plane_data->row_line_count == dl_row_lines)
            {
                // Update scan pointers
                if (dl_instr & MODE_BIT)
                {
                    // update scan pointers to next value
                    plane_data->memory_scan = (uint8_t *)(uintptr_t)interp_get_accumulator(interp0, 0) + 1;
                    plane_data->colour_scan = (uint8_t *)(uintptr_t)interp_get_accumulator(interp1, 0) + 1;
                    plane_data->backgr_scan = (uint8_t *)(uintptr_t)interp_get_accumulator(interp1, 1) + 1;
                }

                // Reset line counter
                plane_data->row_line_count = 0;

                // Move to next DL instruction
                ++plane->offset;
            }
            else
            {
                // Move to next line of current DL row
                ++plane_data->row_line_count;
            }
        }
    }
}

#define SPRITE_PADDING (SPRITE_MAX_WIDTH * CGIA_COLUMN_PX)
#define MIN_SPRITE_X   (-SPRITE_PADDING)
#define MIN_SPRITE_Y   (-EXAMPLE_SPRITE_HEIGHT)

void cgia_vbl(void)
{
    // TODO: trigger CPU NMI

    CGIA.plane[0].regs.bckgnd.scroll++;
}
