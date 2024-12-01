#include "hardware/dma.h"
#include "hardware/interp.h"

#include "cgia.h"
#include "cgia_encode.h"
#include "cgia_palette.h"

#include "images/beast-1.h"
#include "images/beast-2.h"
#include "images/sotb-1.h"
#include "images/sotb-2.h"
#include "images/sotb-3.h"

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
    bool sprites_need_update; // TODO: set when writing CGIA.plane[1].regs.sprite.active
    uint32_t __attribute__((aligned(4))) scanline_buffer[FRAME_CHARS * 3];
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
    CGIA.plane[p].regs.bckgnd.flags = PLANE_MASK_TRANSPARENT | PLANE_MASK_DOUBLE_WIDTH;
    CGIA.plane[p].regs.bckgnd.shared_color[0] = 0;
    CGIA.plane[p].regs.bckgnd.shared_color[1] = 0;
    CGIA.plane[p].regs.bckgnd.row_height = 7;
    CGIA.plane[p].regs.bckgnd.border_columns = 4;
    CGIA.plane[p].regs.bckgnd.scroll = 0;
    CGIA.plane[p].regs.bckgnd.offset = 0;
    CGIA.plane[p].regs.bckgnd.stride = 80;
    for (uint i = 0; i < 25; ++i)
    {
        memcpy(vdo_bank + video_offset_1 + i * 640, bitmap_data_1 + i * 320, 320);
        memcpy(vdo_bank + video_offset_1 + i * 640 + 320, bitmap_data_1 + i * 320, 320);
        memcpy(vdo_bank + color_offset_1 + i * 80, color_data_1 + i * 40, 40);
        memcpy(vdo_bank + color_offset_1 + i * 80 + 40, color_data_1 + i * 40, 40);
        memcpy(vdo_bank + bkgnd_offset_1 + i * 80, bkgnd_data_1 + i * 40, 40);
        memcpy(vdo_bank + bkgnd_offset_1 + i * 80 + 40, bkgnd_data_1 + i * 40, 40);
    }
    memcpy(vdo_bank + dl_offset_1, display_list_1, sizeof(display_list_1));
    CGIA.plane[p].offset = dl_offset_1;

    p = 1;
    CGIA.planes |= (0x01 << p);
    CGIA.plane[p].regs.bckgnd.flags = PLANE_MASK_TRANSPARENT | PLANE_MASK_DOUBLE_WIDTH;
    CGIA.plane[p].regs.bckgnd.shared_color[0] = 0;
    CGIA.plane[p].regs.bckgnd.shared_color[1] = 0;
    CGIA.plane[p].regs.bckgnd.row_height = 7;
    CGIA.plane[p].regs.bckgnd.border_columns = 4;
    CGIA.plane[p].regs.bckgnd.scroll = 0;
    CGIA.plane[p].regs.bckgnd.offset = 0;
    CGIA.plane[p].regs.bckgnd.stride = 80;
    for (uint i = 0; i < 25; ++i)
    {
        memcpy(vdo_bank + video_offset_2 + i * 640, bitmap_data_2 + i * 320, 320);
        memcpy(vdo_bank + video_offset_2 + i * 640 + 320, bitmap_data_2 + i * 320, 320);
        memcpy(vdo_bank + color_offset_2 + i * 80, color_data_2 + i * 40, 40);
        memcpy(vdo_bank + color_offset_2 + i * 80 + 40, color_data_2 + i * 40, 40);
        memcpy(vdo_bank + bkgnd_offset_2 + i * 80, bkgnd_data_2 + i * 40, 40);
        memcpy(vdo_bank + bkgnd_offset_2 + i * 80 + 40, bkgnd_data_2 + i * 40, 40);
    }
    memcpy(vdo_bank + dl_offset_2, display_list_2, sizeof(display_list_2));
    CGIA.plane[p].offset = dl_offset_2;

    p = 2;
    CGIA.planes |= (0x01 << p);
    CGIA.plane[p].regs.bckgnd.flags = PLANE_MASK_TRANSPARENT | PLANE_MASK_DOUBLE_WIDTH;
    CGIA.plane[p].regs.bckgnd.shared_color[0] = 0;
    CGIA.plane[p].regs.bckgnd.shared_color[1] = 0;
    CGIA.plane[p].regs.bckgnd.row_height = 7;
    CGIA.plane[p].regs.bckgnd.border_columns = 4;
    CGIA.plane[p].regs.bckgnd.scroll = 0;
    CGIA.plane[p].regs.bckgnd.offset = 0;
    CGIA.plane[p].regs.bckgnd.stride = 80;
    for (uint i = 0; i < 25; ++i)
    {
        memcpy(vdo_bank + video_offset_3 + i * 640, bitmap_data_3 + i * 320, 320);
        memcpy(vdo_bank + video_offset_3 + i * 640 + 320, bitmap_data_3 + i * 320, 320);
        memcpy(vdo_bank + color_offset_3 + i * 80, color_data_3 + i * 40, 40);
        memcpy(vdo_bank + color_offset_3 + i * 80 + 40, color_data_3 + i * 40, 40);
        memcpy(vdo_bank + bkgnd_offset_3 + i * 80, bkgnd_data_3 + i * 40, 40);
        memcpy(vdo_bank + bkgnd_offset_3 + i * 80 + 40, bkgnd_data_3 + i * 40, 40);
    }
    memcpy(vdo_bank + dl_offset_3, display_list_3, sizeof(display_list_3));
    CGIA.plane[p].offset = dl_offset_3;

    struct cgia_sprite_t *sprites = (struct cgia_sprite_t *)(spr_bank + spr_desc_offset);
    p = 3;
    CGIA.planes |= (0x11 << p);
    CGIA.plane[p].offset = spr_desc_offset;
    CGIA.plane[p].regs.sprite.active = 0b00000011;
    for (uint8_t i = 0; i < 2; ++i)
    {
        sprites[i].flags = SPRITE_MASK_MULTICOLOR;
        sprites[i].flags |= (8) - 1; // width
        sprites[i].lines_y = 52;
        sprites[i].color[0] = 0x99;
        sprites[i].color[1] = 0x9A;
        sprites[i].color[2] = 0x9B;
        sprites[i].pos_x = 180;
        sprites[i].pos_y = 125 + 20;
        sprites[i].next_dsc_offset = spr_desc_offset + i * 16; // point back to self
    }
    sprites[0].color[0] = 0x0A;
    sprites[0].color[1] = 0x6A;
    sprites[0].color[2] = 0x06;
    sprites[1].color[0] = 0x99;
    sprites[1].color[1] = 0x9A;
    sprites[1].color[2] = 0x9B;
    memcpy(spr_bank + spr1_offset, spr_bitmap1_data, sizeof(spr_bitmap1_data));
    memcpy(spr_bank + spr2_offset, spr_bitmap2_data, sizeof(spr_bitmap2_data));
    sprites[0].data_offset = spr2_offset;
    sprites[1].data_offset = spr1_offset;
}

static uint scroll = 0;
static int8_t scroll_moon = 0;
static int8_t offset_moon = 0;
static int8_t scroll_clouds_01 = 0;
static int8_t offset_clouds_01 = 0;
static int8_t scroll_clouds_02 = 0;
static int8_t offset_clouds_02 = 0;
static int8_t scroll_clouds_03 = 0;
static int8_t offset_clouds_03 = 0;
static int8_t scroll_clouds_04 = 0;
static int8_t offset_clouds_04 = 0;
static int8_t scroll_clouds_05 = 0;
static int8_t offset_clouds_05 = 0;
static int8_t scroll_hills_06 = 0;
static int8_t offset_hills_06 = 0;
static int8_t scroll_grass_07 = 0;
static int8_t offset_grass_07 = 0;
static int8_t scroll_trees_08 = 0;
static int8_t offset_trees_08 = 0;
static int8_t scroll_grass_09 = 0;
static int8_t offset_grass_09 = 0;
static int8_t scroll_grass_10 = 0;
static int8_t offset_grass_10 = 0;
static int8_t scroll_grass_11 = 0;
static int8_t offset_grass_11 = 0;
static int8_t scroll_fence_12 = 0;
static int8_t offset_fence_12 = 0;

void fake_dli_handler(uint y)
{
    switch (y - 20) // subtract top border size
    {
    case 0:
        CGIA.plane[0].regs.bckgnd.scroll = scroll_moon;
        CGIA.plane[0].regs.bckgnd.offset = offset_moon;
        CGIA.plane[1].regs.bckgnd.scroll = scroll_clouds_01;
        CGIA.plane[1].regs.bckgnd.offset = offset_clouds_01;
        CGIA.plane[2].regs.bckgnd.scroll = scroll_trees_08;
        CGIA.plane[2].regs.bckgnd.offset = offset_trees_08;
        break;
    case 21:
        CGIA.plane[1].regs.bckgnd.scroll = scroll_clouds_02;
        CGIA.plane[1].regs.bckgnd.offset = offset_clouds_02;
        break;
    case 61:
        CGIA.plane[1].regs.bckgnd.scroll = scroll_clouds_03;
        CGIA.plane[1].regs.bckgnd.offset = offset_clouds_03;
        break;
    case 72:
        CGIA.plane[0].regs.bckgnd.scroll = scroll_hills_06;
        CGIA.plane[0].regs.bckgnd.offset = offset_hills_06;
        break;
    case 76:
        CGIA.back_color = 0x9b;
        break;
    case 80:
        CGIA.plane[1].regs.bckgnd.scroll = scroll_clouds_04;
        CGIA.plane[1].regs.bckgnd.offset = offset_clouds_04;
        break;
    case 89:
        CGIA.plane[1].regs.bckgnd.scroll = scroll_clouds_05;
        CGIA.plane[1].regs.bckgnd.offset = offset_clouds_05;
        break;
    case 96:
        CGIA.plane[1].regs.bckgnd.scroll = scroll_grass_07;
        CGIA.plane[1].regs.bckgnd.offset = offset_grass_07;
        break;
    case 103:
        CGIA.back_color = 0xa4;
        break;
    case 117:
        CGIA.back_color = 0xb4;
        break;
    case 127:
        CGIA.back_color = 0xc4;
        break;
    case 135:
        CGIA.back_color = 0xcd;
        break;
    case 142:
        CGIA.back_color = 0xdd;
        break;
    case 148:
        CGIA.back_color = 0xed;
        break;
    case 154:
        CGIA.back_color = 0xf6;
        break;
    case 158:
        CGIA.back_color = 0x0e;
        break;
    case 175:
        CGIA.plane[0].regs.bckgnd.scroll = scroll_grass_09;
        CGIA.plane[0].regs.bckgnd.offset = offset_grass_09;
        break;
    case 178:
        CGIA.plane[1].regs.bckgnd.scroll = scroll_fence_12;
        CGIA.plane[1].regs.bckgnd.offset = offset_fence_12;
        break;
    case 182:
        CGIA.plane[0].regs.bckgnd.scroll = scroll_grass_10;
        CGIA.plane[0].regs.bckgnd.offset = offset_grass_10;
        break;
    case 189:
        CGIA.plane[0].regs.bckgnd.scroll = scroll_grass_11;
        CGIA.plane[0].regs.bckgnd.offset = offset_grass_11;
        break;
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

static uint8_t __attribute__((aligned(4))) log2[256] = {
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

            uint8_t *bckgnd_bank = vdo_bank; // psram + (CGIA.bckgnd_bank << 16)

        process_instruction:
            uint8_t dl_instr = bckgnd_bank[plane->offset];
            uint8_t instr_code = dl_instr & 0b00001111;

            // If it is a blank line (INSTR0) or first plane is transparent
            // fill the whole line with background color
            if (instr_code == 0 || (!line_background_filled && plane->regs.bckgnd.flags & PLANE_MASK_TRANSPARENT))
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
                        plane_data->wait_vbl = true;
                        continue;
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
                        uint8_t char_shift = log2[plane->regs.bckgnd.row_height];
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
                        uint8_t char_shift = log2[plane->regs.bckgnd.row_height];
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
                        offset_delta <<= log2[row_height];
                        interp_set_accumulator(interp0, 0, (uintptr_t)plane_data->memory_scan + offset_delta);
                        interp_set_base(interp0, 0, ++row_height);
                    }
                    if (row_columns)
                    {
                        int8_t scr_delta = plane->regs.bckgnd.scroll;
                        if (scr_delta < 0)
                            scr_delta -= 7;
                        const uint8_t encode_columns = (uint8_t)(row_columns - scr_delta / CGIA_COLUMN_PX);
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
        }
    }

    fake_dli_handler((y + 1) % FRAME_HEIGHT); // FIXME: remove me
}

#define SCROLL_MAX 9600

void cgia_vbl(void)
{
    // TODO: trigger CPU NMI

    // ------- fake VBL --- FIXME: remove this
    CGIA.back_color = 0x8b;

    scroll += 3;
    if (scroll >= SCROLL_MAX)
        scroll = 0;

    // 01: 3300px
    const uint scroll_01 = (scroll * 3520 / SCROLL_MAX) % 320;
    scroll_clouds_01 = -(scroll_01 % 8);
    offset_clouds_01 = (int8_t)(scroll_01 / 8);
    // 02: 2700px
    const uint scroll_02 = (scroll * 2880 / SCROLL_MAX) % 320;
    scroll_clouds_02 = -(scroll_02 % 8);
    offset_clouds_02 = (int8_t)(scroll_02 / 8);
    // 03: 2500
    const uint scroll_03 = (scroll * 2560 / SCROLL_MAX) % 320;
    scroll_clouds_03 = -(scroll_03 % 8);
    offset_clouds_03 = (int8_t)(scroll_03 / 8);
    // 04: 2200
    const uint scroll_04 = (scroll * 2240 / SCROLL_MAX) % 320;
    scroll_clouds_04 = -(scroll_04 % 8);
    offset_clouds_04 = (int8_t)(scroll_04 / 8);
    // 05: 2000
    const uint scroll_05 = (scroll * 1920 / SCROLL_MAX) % 320;
    scroll_clouds_05 = -(scroll_05 % 8);
    offset_clouds_05 = (int8_t)(scroll_05 / 8);
    // 06: 2700
    const uint scroll_06 = (scroll * 2880 / SCROLL_MAX) % 320;
    scroll_hills_06 = -(scroll_06 % 8);
    offset_hills_06 = (int8_t)(scroll_06 / 8);
    // 07: 3400
    const uint scroll_07 = (scroll * 3520 / SCROLL_MAX) % 320;
    scroll_grass_07 = -(scroll_07 % 8);
    offset_grass_07 = (int8_t)(scroll_07 / 8);
    // 08: 4500
    const uint scroll_08 = (scroll * 4480 / SCROLL_MAX) % 320;
    scroll_trees_08 = -(scroll_08 % 8);
    offset_trees_08 = (int8_t)(scroll_08 / 8);
    // 09: 5400
    const uint scroll_09 = (scroll * 5440 / SCROLL_MAX) % 320;
    scroll_grass_09 = -(scroll_09 % 8);
    offset_grass_09 = (int8_t)(scroll_09 / 8);
    // 10: 6800
    const uint scroll_10 = (scroll * 6720 / SCROLL_MAX) % 320;
    scroll_grass_10 = -(scroll_10 % 8);
    offset_grass_10 = (int8_t)(scroll_10 / 8);
    // 11: 8200
    const uint scroll_11 = (scroll * 8320 / SCROLL_MAX) % 320;
    scroll_grass_11 = -(scroll_11 % 8);
    offset_grass_11 = (int8_t)(scroll_11 / 8);
    // 12: 9600
    const uint scroll_12 = (scroll * 9600 / SCROLL_MAX) % 320;
    scroll_fence_12 = -(scroll_12 % 8);
    offset_fence_12 = (int8_t)(scroll_12 / 8);

    uint8_t sprite_frame = (scroll / 20) % 6;
    struct cgia_sprite_t *sprites = (struct cgia_sprite_t *)(spr_bank + spr_desc_offset);
    sprites[0].data_offset = spr2_offset + (uint16_t)(sprite_frame * 8 * 52);
    sprites[1].data_offset = spr1_offset + (uint16_t)(sprite_frame * 8 * 52);
}
