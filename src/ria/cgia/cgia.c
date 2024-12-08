#include "hardware/dma.h"
#include "hardware/interp.h"

#include "cgia.h"
#include "cgia_encode.h"
#include "cgia_palette.h"

#include "example_data.h"
#include "font_8.h"

#include "sys/out.h"

#include <math.h>
#include <stdio.h>
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
    interp_hw_save_t interpolator[2];
    bool wait_vbl;
    bool sprites_need_update; // TODO: set when writing CGIA.plane[1].regs.sprite.active

    // work buffer for scanline - used to prepare data for rasterizer
    // bitmap,fg,bg ; *2 because of hires mode
    // TODO: remove it - pull data directly from interpolators
    uint32_t __attribute__((aligned(4))) scanline_buffer[FRAME_CHARS * 3 * 2];
}
__attribute__((aligned(4))) plane_int[CGIA_PLANES]
    = {0};

static uint16_t __attribute__((aligned(4))) sprite_dsc_offsets[CGIA_PLANES][CGIA_SPRITES] = {0};
static uint8_t __attribute__((aligned(4))) sprite_line_data[SPRITE_MAX_WIDTH];

// --- temporary WIP ---
uint8_t __attribute__((aligned(4))) vdo_bank[256 * 256];
uint8_t __attribute__((aligned(4))) spr_bank[256 * 256];

// ---
#define EXAMPLE_BORDER_COLOR 145
#define EXAMPLE_TEXT_COLOR   150

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

int16_t __attribute__((aligned(4))) sin_tab[256];
int16_t __attribute__((aligned(4))) cos_tab[256];

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

    for (int i = 0; i < 256; ++i)
    {
        sin_tab[i] = (int16_t)(sin(i * (M_PI / 128)) * 256.);
        cos_tab[i] = (int16_t)(cos(i * (M_PI / 128)) * 256.);
    }

    p = 0;
    CGIA.planes |= (0x01 << p);
    CGIA.plane[p].regs.bckgnd.flags = 0;
    CGIA.plane[p].regs.bckgnd.row_height = 7;
    CGIA.plane[p].regs.bckgnd.border_columns = 0;
    for (int i = 0; i < 96 * 30; ++i)
    {
        (vdo_bank + text_mode_video_offset)[i] = 0;
        (vdo_bank + text_mode_color_offset)[i] = 6;
        (vdo_bank + text_mode_bkgnd_offset)[i] = 0;
    }
    memcpy(vdo_bank + text_mode_chrgn_offset, font8_data, sizeof(font8_data));
    memcpy(vdo_bank + text_mode_dl_offset, text80_mode_dl, sizeof(text80_mode_dl));
    sprintf((char *)(vdo_bank + text_mode_video_offset), "READY");
    sprintf((char *)(vdo_bank + text_mode_video_offset + 96 * 10 + 2), "Lorem ipsum dolor sit amet, consectetur adipiscing elt, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.");
    CGIA.plane[p].offset = text_mode_dl_offset;
}

static uint8_t __attribute__((aligned(4))) log2_tab[256] = {
    0x00, 0x01, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, //
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, //
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, //
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, //
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, //
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, //
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, //
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, //
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, //
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, //
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, //
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, //
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, //
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, //
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, //
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, //
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, //
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, //
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, //
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, //
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, //
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, //
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, //
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, //
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, //
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, //
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, //
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, //
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, //
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, //
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, //
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, //
};

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

void __not_in_flash_func(cgia_render)(uint y, uint32_t *rgbbuf)
{
    static struct cgia_plane_t *plane;
    static struct cgia_plane_internal *plane_data;
    static uint16_t(*sprite_dscs)[CGIA_SPRITES];

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
            plane_data = plane_int + p;
            sprite_dscs = &sprite_dsc_offsets[p];
            uint8_t *sprite_bank = spr_bank; // psram + (CGIA.sprite_bank << 16)

            if (y == 0 // start of frame - reload descriptors
                || plane_data->sprites_need_update)
            {
                uint16_t offs = plane->offset;
                (*sprite_dscs)[0] = offs;
                offs += 16;
                (*sprite_dscs)[1] = offs;
                offs += 16;
                (*sprite_dscs)[2] = offs;
                offs += 16;
                (*sprite_dscs)[3] = offs;
                offs += 16;
                (*sprite_dscs)[4] = offs;
                offs += 16;
                (*sprite_dscs)[5] = offs;
                offs += 16;
                (*sprite_dscs)[6] = offs;
                offs += 16;
                (*sprite_dscs)[7] = offs;
            }

            // render sprites in reverse order
            // so lower indexed sprites have higher visual priority
            uint8_t sprite_index = 7;
            uint8_t mask = 0b10000000;

            // wait until back fill is done, as it may overwrite sprites on the right side
            dma_channel_wait_for_finish_blocking(back_chan);

            while (mask)
            {
                if (plane->regs.sprite.active & mask)
                {
                    struct cgia_sprite_t *sprite = (struct cgia_sprite_t *)(sprite_bank + (*sprite_dscs)[sprite_index]);

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

                        // if this was the last line of sprite, load the next offset
                        if (sprite->pos_y + sprite->lines_y == (int)y + 1)
                        {
                            (*sprite_dscs)[sprite_index] = sprite->next_dsc_offset;
                        }
                    }
                }
                --sprite_index;
                mask >>= 1;
            }
        }
        else
        {
            /* --- BACKGROUND --- */
            plane = CGIA.plane + p;
            plane_data = plane_int + p;

        restart_plane:
            if (y == 0) // start of frame - reset flags and counters
            {
                plane_data->wait_vbl = false;
                plane_data->row_line_count = 0;
            }

            if (!(CGIA.planes & (1u << p)))
                continue; // next if not enabled

            // restore interpolators state for this plane
            interp_restore(interp0, &plane_data->interpolator[0]);
            interp_restore(interp1, &plane_data->interpolator[1]);

        process_instruction:
            if (plane_data->wait_vbl && y != 0)
            {
                // DL is stopped and waiting for VBL
                if (!(plane->regs.bckgnd.flags & PLANE_MASK_BORDER_TRANSPARENT))
                {
                    // generate full-length border line
                    (void)fill_back(rgbbuf, DISPLAY_WIDTH_PIXELS / CGIA_COLUMN_PX, CGIA.back_color);
                    line_background_filled = true;
                }
                // and we're done
                goto next_plane;
            }

            uint8_t *bckgnd_bank = vdo_bank; // psram + (CGIA.bckgnd_bank << 16)
            uint8_t dl_instr = bckgnd_bank[plane->offset];
            uint8_t instr_code = dl_instr & 0b00001111;

            // If it is a blank line (INSTR0) or plane is transparent
            // fill the whole line with background color
            if ((instr_code == 0 && !(plane->regs.bckgnd.flags & PLANE_MASK_BORDER_TRANSPARENT)) || (!line_background_filled && plane->regs.bckgnd.flags & PLANE_MASK_TRANSPARENT))
            {
                (void)fill_back(rgbbuf, DISPLAY_WIDTH_PIXELS / CGIA_COLUMN_PX, CGIA.back_color);
            }

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
                    {
                        // if DLI set, wait for VBL
                        plane_data->wait_vbl = true;
                        goto restart_plane;
                    }

                    // .display_list is already pointing to proper instruction
                    goto process_instruction;

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
                    ++plane->offset; // Move to next DL instruction
                    goto process_instruction;

                case 0x4: // Set 8-bit register
                {
                    ((uint8_t *)&plane->regs)[(dl_instr & 0b01110000) >> 4] = bckgnd_bank[++plane->offset];
                }
                    ++plane->offset; // Move to next DL instruction
                    goto process_instruction;

                case 0x5: // Set 16-bit register
                {
                    ((uint8_t *)&plane->regs)[(dl_instr & 0b01110000) >> 3] = (uint16_t)((bckgnd_bank[++plane->offset])
                                                                                         | (bckgnd_bank[++plane->offset] << 8));
                }
                    ++plane->offset; // Move to next DL instruction
                    goto process_instruction;

                // ------- UNKNOWN INSTRUCTION
                default:
                    (void)fill_back(rgbbuf, DISPLAY_WIDTH_PIXELS / CGIA_COLUMN_PX, 234);
                    dl_row_lines = plane_data->row_line_count; // force moving to next DL instruction
                    goto plane_epilogue;
                }
            }
            else
            {
                uint8_t border_columns = plane->regs.bckgnd.border_columns;
                if (border_columns > MAX_BORDER_COLUMNS)
                    border_columns = MAX_BORDER_COLUMNS;
                uint8_t row_columns = FRAME_CHARS - 2 * border_columns;

                // Used for tracking where to blit pixel data
                uint32_t *buf = rgbbuf + border_columns * CGIA_COLUMN_PX;
                if (instr_code != (0x7 | MODE_BIT))
                {
                    buf += plane->regs.bckgnd.scroll;

                    // If it is not MODE7, use dfault interpolator configuration
                    // TODO: possibly MODE7 config could be used for normal operation
                    interp_config cfg = interp_default_config();
                    interp_config_set_add_raw(&cfg, true);
                    interp_set_config(interp0, 0, &cfg);
                    interp_set_config(interp0, 1, &cfg);
                    interp_set_config(interp1, 0, &cfg);
                    interp_set_config(interp1, 1, &cfg);
                    interp_set_base(interp1, 0, 1);
                    interp_set_base(interp1, 1, 1);
                }

                // ------- Mode Rows --------------
                switch (instr_code)
                {
                case (0x0 | MODE_BIT): // MODE0 (8) - text80 mode
                {
                    interp_set_base(interp0, 0, 1);
                    interp_set_accumulator(interp0, 0, (uintptr_t)plane_data->memory_scan - 1);
                    interp_set_accumulator(interp1, 0, (uintptr_t)plane_data->colour_scan - 1);
                    interp_set_accumulator(interp1, 1, (uintptr_t)plane_data->backgr_scan - 1);
                    if (row_columns)
                    {
                        uint8_t encode_columns = (uint8_t)(row_columns << 1);
                        uint8_t char_shift = log2_tab[plane->regs.bckgnd.row_height];
                        load_textmode_buffer(plane_data->scanline_buffer, encode_columns, plane_data->char_gen + plane_data->row_line_count, char_shift);
                        buf = cgia_encode_mode_3_mapped(buf, plane_data->scanline_buffer, encode_columns);
                    }
                }
                break;

                case (0x2 | MODE_BIT): // MODE2 (A) - text/tile mode
                {
                    interp_set_base(interp0, 0, 1);
                    interp_set_accumulator(interp0, 0, (uintptr_t)plane_data->memory_scan - 1);
                    interp_set_accumulator(interp1, 0, (uintptr_t)plane_data->colour_scan - 1);
                    interp_set_accumulator(interp1, 1, (uintptr_t)plane_data->backgr_scan - 1);
                    if (row_columns)
                    {
                        uint8_t char_shift = log2_tab[plane->regs.bckgnd.row_height];
                        if (plane->regs.bckgnd.flags & PLANE_MASK_TRANSPARENT)
                        {
                            buf = cgia_encode_mode_2_shared(buf, row_columns, plane_data->char_gen + plane_data->row_line_count, char_shift);
                        }
                        else
                        {
                            buf = cgia_encode_mode_2_mapped(buf, row_columns, plane_data->char_gen + plane_data->row_line_count, char_shift);
                        }
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

                        // next raster line starts with next byte, but color/bg scan stay the same
                        ++plane_data->memory_scan;
                    }
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
                        uint8_t char_shift = log2_tab[plane->regs.bckgnd.row_height];
                        load_textmode_buffer(plane_data->scanline_buffer, row_columns, plane_data->char_gen + plane_data->row_line_count, char_shift);
                        if (plane->regs.bckgnd.flags & PLANE_MASK_TRANSPARENT)
                        {
                            if (plane->regs.bckgnd.flags & PLANE_MASK_DOUBLE_WIDTH)
                                buf = cgia_encode_mode_5_doubled_shared(buf, plane_data->scanline_buffer, row_columns, plane->regs.bckgnd.shared_color);
                            else
                                buf = cgia_encode_mode_5_shared(buf, plane_data->scanline_buffer, row_columns, plane->regs.bckgnd.shared_color);
                        }
                        else
                        {
                            if (plane->regs.bckgnd.flags & PLANE_MASK_DOUBLE_WIDTH)
                                buf = cgia_encode_mode_5_doubled_mapped(buf, plane_data->scanline_buffer, row_columns, plane->regs.bckgnd.shared_color);
                            else
                                buf = cgia_encode_mode_5_mapped(buf, plane_data->scanline_buffer, row_columns, plane->regs.bckgnd.shared_color);
                        }
                    }
                }
                break;

                case (0x5 | MODE_BIT): // MODE5 (D) - multicolor bitmap mode
                {
                    {
                        int offset_delta = plane->regs.bckgnd.offset - 1;
                        interp_set_accumulator(interp1, 0, (uintptr_t)plane_data->colour_scan + offset_delta);
                        interp_set_accumulator(interp1, 1, (uintptr_t)plane_data->backgr_scan + offset_delta);
                        uint8_t row_height = plane->regs.bckgnd.row_height;
                        offset_delta <<= log2_tab[row_height];
                        interp_set_accumulator(interp0, 0, (uintptr_t)plane_data->memory_scan + offset_delta);
                        interp_set_base(interp0, 0, ++row_height);
                    }
                    if (row_columns)
                    {
                        uint8_t encode_columns = row_columns;
                        if (plane->regs.bckgnd.stride)
                        {
                            int8_t scr_delta = plane->regs.bckgnd.scroll;
                            if (scr_delta < 0)
                                scr_delta -= 7;
                            encode_columns = (uint8_t)(encode_columns - scr_delta / CGIA_COLUMN_PX);
                        }
                        if (!(plane->regs.bckgnd.flags & PLANE_MASK_DOUBLE_WIDTH))
                        {
                            // this mode generates 4x8 cells, so requires 2x columns
                            encode_columns <<= 1;
                        }
                        load_scanline_buffer_mapped(plane_data->scanline_buffer, encode_columns);
                        if (plane->regs.bckgnd.flags & PLANE_MASK_TRANSPARENT)
                        {
                            if (plane->regs.bckgnd.flags & PLANE_MASK_DOUBLE_WIDTH)
                                buf = cgia_encode_mode_5_doubled_shared(buf, plane_data->scanline_buffer, encode_columns, plane->regs.bckgnd.shared_color);
                            else
                                buf = cgia_encode_mode_5_shared(buf, plane_data->scanline_buffer, encode_columns, plane->regs.bckgnd.shared_color);
                        }
                        else
                        {
                            if (plane->regs.bckgnd.flags & PLANE_MASK_DOUBLE_WIDTH)
                                buf = cgia_encode_mode_5_doubled_mapped(buf, plane_data->scanline_buffer, encode_columns, plane->regs.bckgnd.shared_color);
                            else
                                buf = cgia_encode_mode_5_mapped(buf, plane_data->scanline_buffer, encode_columns, plane->regs.bckgnd.shared_color);
                        }

                        // next raster line starts with next byte, but color/bg scan stay the same
                        ++plane_data->memory_scan;
                    }
                }
                break;

                case (0x7 | MODE_BIT): // MODE7 (F) - affine transform mode
                {
                    if (row_columns)
                    {
                        if (plane_data->row_line_count == 0)
                        {
                            interp_config cfg = interp_default_config();
                            interp_config_set_add_raw(&cfg, true);

                            // interp0 will scan texture row
                            // interp1 will scan row begin address
                            const uint texture_width_bits = plane->regs.affine.texture_bits & 0b0111;
                            interp_config_set_shift(&cfg, CGIA_AFFINE_FRACTIONAL_BITS);
                            interp_config_set_mask(&cfg, 0, texture_width_bits - 1);
                            interp_set_config(interp0, 0, &cfg);
                            const uint texture_height_bits = (plane->regs.affine.texture_bits >> 4) & 0b0111;
                            interp_config_set_shift(&cfg, CGIA_AFFINE_FRACTIONAL_BITS - texture_width_bits);
                            interp_config_set_mask(&cfg, texture_width_bits, texture_width_bits + texture_height_bits - 1);
                            interp_set_config(interp0, 1, &cfg);

                            // interp1 will scan row begin address
                            interp_config_set_shift(&cfg, CGIA_AFFINE_FRACTIONAL_BITS);
                            interp_config_set_mask(&cfg, 0, texture_width_bits - 1);
                            interp_set_config(interp1, 0, &cfg);
                            interp_config_set_shift(&cfg, 0);
                            interp_config_set_mask(&cfg, CGIA_AFFINE_FRACTIONAL_BITS, CGIA_AFFINE_FRACTIONAL_BITS + texture_height_bits - 1);
                            interp_set_config(interp1, 1, &cfg);
                            interp1->accum[0] = plane->regs.affine.u;
                            interp1->base[0] = plane->regs.affine.dx;
                            interp1->accum[1] = plane->regs.affine.v;
                            interp1->base[1] = plane->regs.affine.dy;
                            interp1->base[2] = 0;
                        }

                        interp0->base[2] = (uintptr_t)plane_data->memory_scan;
                        const uint32_t xy = interp1->pop[2];
                        interp0->accum[0] = (xy & 0x00FF) << CGIA_AFFINE_FRACTIONAL_BITS;
                        interp0->base[0] = plane->regs.affine.du;
                        interp0->accum[1] = (xy & 0xFF00);
                        interp0->base[1] = plane->regs.affine.dv;

                        buf = cgia_encode_mode_7(buf, row_columns);
                    }
                }
                break;

                // ------- UNKNOWN MODE - generate pink line (should not happen)
                default:
                    (void)fill_back(rgbbuf, DISPLAY_WIDTH_PIXELS / CGIA_COLUMN_PX, 234);
                    dl_row_lines = plane_data->row_line_count; // force moving to next DL instruction
                    goto plane_epilogue;
                }

                // borders
                if ((dl_instr & MODE_BIT) && border_columns && !(plane->regs.bckgnd.flags & PLANE_MASK_BORDER_TRANSPARENT))
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
                if (dl_instr & MODE_BIT && instr_code != (0x7 | MODE_BIT))
                {
                    // update scan pointers to next value
                    uint8_t stride = plane->regs.bckgnd.stride;
                    if (stride)
                    {
                        plane_data->colour_scan += stride;
                        plane_data->backgr_scan += stride;
                        plane_data->memory_scan += --stride * (plane->regs.bckgnd.row_height + 1);
                    }
                    else
                    {
                        plane_data->memory_scan = (uint8_t *)(uintptr_t)interp_get_accumulator(interp0, 0) + 1;
                        plane_data->colour_scan = (uint8_t *)(uintptr_t)interp_get_accumulator(interp1, 0) + 1;
                        plane_data->backgr_scan = (uint8_t *)(uintptr_t)interp_get_accumulator(interp1, 1) + 1;
                    }
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
        next_plane:
            // save interpolators state for this plane
            interp_save(interp0, &plane_data->interpolator[0]);
            interp_save(interp1, &plane_data->interpolator[1]);
        }
    }
}

static uint frame = 0;
void cgia_vbl(void)
{
    // TODO: trigger CPU NMI

    if (frame % 60 == 0)
    {
        // blink cursor
        uint16_t cursor_offset = (FRAME_CHARS - 2 * CGIA.plane[0].regs.bckgnd.border_columns) * 2;
        uint8_t bg = (vdo_bank + text_mode_bkgnd_offset)[cursor_offset];
        (vdo_bank + text_mode_bkgnd_offset)[cursor_offset] = (vdo_bank + text_mode_color_offset)[cursor_offset];
        (vdo_bank + text_mode_color_offset)[cursor_offset] = bg;
    }

    ++frame;
}
