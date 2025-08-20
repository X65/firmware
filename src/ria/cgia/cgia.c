#include "pico.h"
#ifdef PICO_SDK_VERSION_MAJOR
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/interp.h"

#include "cgia.h"
#include "cgia_encode.h"
#define CGIA_PALETTE_IMPL
#include "cgia_palette.h"
#include "main.h"
#include "term/term.h"

#include "sys/aud.h"
// #include "sys/mem.h"
#include "sys/out.h"
#endif

#include <string.h>

#define DISPLAY_WIDTH_PIXELS (MODE_H_ACTIVE_PIXELS / FB_H_REPEAT)
#define MAX_BORDER_COLUMNS   (DISPLAY_WIDTH_PIXELS / CGIA_COLUMN_PX / 2 /* borders */)

#define FRAME_CHARS (DISPLAY_WIDTH_PIXELS / CGIA_COLUMN_PX)

#define DISPLAY_HEIGHT_LINES (MODE_V_ACTIVE_LINES / FB_H_REPEAT)

#define UNHANDLED_DL_COLOR (234)

#define CGIA_REGS_NO ((CGIA_PLANE_REGS_NO * 4) << 1)
_Static_assert(CGIA_REGS_NO == sizeof(struct cgia_t), "Incorrect CGIA_REGS_NO");

// --- Globals ---
// two "banks" to mirror PSRAM content for fast CGIA access
uint8_t
    __attribute__((aligned(4)))
    vram_cache[CGIA_VRAM_BANKS][0x10000];

uint8_t
    __attribute__((aligned(4)))
    __scratch_x("")
        regs_int[CGIA_REGS_NO]
    = {0};
#define CGIA (*((struct cgia_t *)regs_int))

struct cgia_plane_internal
{
    uint16_t memory_scan;
    uint16_t colour_scan;
    uint16_t backgr_scan;
    uint16_t char_gen_offset;
    uint8_t row_line_count;
    interp_hw_save_t interpolator[2];
    bool wait_vbl;
    bool sprites_need_update;
};
struct cgia_plane_internal
    __attribute__((aligned(4)))
    __scratch_x("")
        plane_int[CGIA_PLANES]
    = {0};

static uint16_t
    __attribute__((aligned(4)))
    __scratch_x("")
        sprite_dsc_offsets[CGIA_PLANES][CGIA_SPRITES]
    = {0};
static uint8_t
    __attribute__((aligned(4)))
    __scratch_x("")
        sprite_line_data[SPRITE_MAX_WIDTH];

// store which PSRAM bank is currently mirrored in cache
// stored as bitmask for easy use during ram write call
uint32_t
    __attribute__((aligned(4)))
    __scratch_x("")
        vram_cache_bank_mask[CGIA_VRAM_BANKS]
    = {0, 0};

// store in which vram cache bank a cgia bank (backgnd/sprite) is stored
// these may be shared - backgnd and sprites in same bank
uint8_t *
    __scratch_x("")
        vram_cache_ptr[CGIA_VRAM_BANKS]
    = {vram_cache[0], vram_cache[0]};

inline __attribute__((always_inline)) __attribute__((optimize("O3"))) void cgia_ram_write(uint32_t addr, uint8_t data)
{
    const uint32_t bank_mask = addr & 0xFFFF0000;
    if (bank_mask == vram_cache_bank_mask[0])
    {
        vram_cache_ptr[0][addr & 0xFFFF] = data;
    }
    if (bank_mask == vram_cache_bank_mask[1])
    {
        vram_cache_ptr[1][addr & 0xFFFF] = data;
    }
}

// store which memory bank is wanted in vram cache bank
// used to trigger DMA transfer during cgia_run() workloop
uint32_t
    __attribute__((aligned(4)))
    __scratch_x("")
        vram_wanted_bank_mask[CGIA_VRAM_BANKS]
    = {0, 0};

void cgia_set_bank(uint8_t cgia_bank_id, uint8_t mem_bank_no)
{
    assert(cgia_bank_id < 2);
    const uint32_t bank_mask = mem_bank_no << 16;
    vram_wanted_bank_mask[cgia_bank_id] = bank_mask;

    if (bank_mask == vram_cache_bank_mask[cgia_bank_id])
    {
        // if the bank matches - nothing to do
        return;
    }
    // if the new bank_no matches the one in other bank, re-use it
    const uint8_t other_bank_id = cgia_bank_id ^ 1;
    if (bank_mask == vram_cache_bank_mask[other_bank_id])
    {
        vram_cache_bank_mask[cgia_bank_id] = bank_mask;
        vram_cache_ptr[cgia_bank_id] = vram_cache_ptr[other_bank_id];
        return;
    }

    // if we got here, the vram_wanted_bank_mask differs from vram_cache_bank_mask
    // which will trigger memory bank switch in work loop
    // our job here is done
}

// This mask is used to enable interruptable render points in time.
// Once ACK'ed, the interrupt source will not be signalled until
// the condition in met again
// VBL - new frame starts
// RSI - new raster line starts
// DLI - next DL instruction is loaded
uint8_t int_mask = 0;

#define INT_STATUS_MASKED (regs_int[CGIA_REG_INT_STATUS] & regs_int[CGIA_REG_INT_ENABLE] & int_mask)

inline __attribute__((always_inline)) __attribute__((optimize("O3"))) void cgia_vbi(void)
{
    int_mask |= CGIA_REG_INT_FLAG_VBI;

    if (CGIA.int_enable & CGIA_REG_INT_FLAG_VBI)
    {
        CGIA.int_status |= CGIA_REG_INT_FLAG_VBI;
    }
}

inline __attribute__((always_inline)) __attribute__((optimize("O3"))) uint8_t cgia_reg_read(uint8_t reg_no)
{
    const uint8_t reg = reg_no & 0x7F;
    switch (reg)
    {
    case CGIA_REG_INT_STATUS:
        return INT_STATUS_MASKED;
    }

    return regs_int[reg];
}

inline __attribute__((always_inline)) __attribute__((optimize("O3"))) void cgia_reg_write(uint8_t reg_no, uint8_t value)
{
    const uint8_t reg = reg_no & 0x7F;
    regs_int[reg] = value;

    switch (reg)
    {
    case CGIA_REG_BCKGND_BANK:
        cgia_set_bank(0, value);
        break;
    case CGIA_REG_SPRITE_BANK:
        cgia_set_bank(1, value);
        break;
    case CGIA_REG_INT_ENABLE:
        regs_int[reg] = value & 0b11100000;
        int_mask &= ~(value & 0b11100000);
        break;
    case CGIA_REG_INT_STATUS:
        CGIA.int_status = 0x00;
        int_mask = 0x00;
        break;

    case CGIA_REG_PLANES + CGIA_PLANE_REGS_NO * 0: // .plane[0].sprite.active ?
        if (CGIA.planes & (0x10 << 0))
            plane_int[0].sprites_need_update = true;
        break;
    case CGIA_REG_PLANES + CGIA_PLANE_REGS_NO * 1: // .plane[1].sprite.active ?
        if (CGIA.planes & (0x10 << 1))
            plane_int[1].sprites_need_update = true;
        break;
    case CGIA_REG_PLANES + CGIA_PLANE_REGS_NO * 2: // .plane[2].sprite.active ?
        if (CGIA.planes & (0x10 << 2))
            plane_int[2].sprites_need_update = true;
        break;
    case CGIA_REG_PLANES + CGIA_PLANE_REGS_NO * 3: // .plane[3].sprite.active ?
        if (CGIA.planes & (0x10 << 3))
            plane_int[3].sprites_need_update = true;
        break;
    }
}

static inline __attribute__((always_inline)) void cpu_set_nmi(void)
{
    gpio_put(RIA_NMIB_PIN, !INT_STATUS_MASKED);
}

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
    memset(&CGIA, 0, CGIA_REGS_NO);
    memset(plane_int, 0, sizeof(plane_int));
    memset(sprite_dsc_offsets, 0, sizeof(sprite_dsc_offsets));

    for (uint i = 0; i < CGIA_PLANES; ++i)
    {
        // All planes should initially wait for VBL
        plane_int[i].wait_vbl = true;
        // And update sprite descriptors
        plane_int[i].sprites_need_update = true;
    }

#ifdef PICO_SDK_VERSION_MAJOR
    // DMA
    ctrl_chan
        = dma_claim_unused_channel(true);
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
#endif
}

static uint8_t
    __attribute__((aligned(4)))
    // __scratch_x("")
    log2_tab[256]
    = {
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

#ifdef PICO_SDK_VERSION_MAJOR
static inline __attribute__((always_inline)) uint32_t *fill_back(
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

static inline __attribute__((always_inline)) void set_linear_scans(
    uint8_t row_height,
    const uint8_t *memory_scan,
    const uint8_t *colour_scan,
    const uint8_t *backgr_scan)
{
    // TODO: unwrap these function calls to direct memory writes
    interp_config cfg = interp_default_config();
    interp_config_set_add_raw(&cfg, true);

    interp_set_config(interp0, 0, &cfg);
    interp_set_base(interp0, 0, row_height);
    interp_set_accumulator(interp0, 0, (uintptr_t)memory_scan);

    interp_set_config(interp1, 0, &cfg);
    interp_set_base(interp1, 0, 1);
    interp_set_accumulator(interp1, 0, (uintptr_t)colour_scan);

    interp_set_config(interp1, 1, &cfg);
    interp_set_base(interp1, 1, 1);
    interp_set_accumulator(interp1, 1, (uintptr_t)backgr_scan);
}

static inline __attribute__((always_inline)) void set_mode7_interp_config(union cgia_plane_regs_t *plane)
{
    interp_config cfg = interp_default_config();
    interp_config_set_add_raw(&cfg, true);

    // interp0 will scan texture row
    // interp1 will scan row begin address
    const uint texture_width_bits = plane->affine.texture_bits & 0b0111;
    interp_config_set_shift(&cfg, CGIA_AFFINE_FRACTIONAL_BITS);
    interp_config_set_mask(&cfg, 0, texture_width_bits - 1);
    interp_set_config(interp0, 0, &cfg);
    const uint texture_height_bits = (plane->affine.texture_bits >> 4) & 0b0111;
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
    interp1->accum[0] = plane->affine.u;
    interp1->base[0] = plane->affine.dx;
    interp1->accum[1] = plane->affine.v;
    interp1->base[1] = plane->affine.dy;
    interp1->base[2] = 0;
}
static inline __attribute__((always_inline)) void set_mode7_scans(union cgia_plane_regs_t *plane, uint8_t *memory_scan)
{
    interp0->base[2] = (uintptr_t)memory_scan;
    const uint32_t xy = interp1->pop[2];
    interp0->accum[0] = (xy & 0x00FF) << CGIA_AFFINE_FRACTIONAL_BITS;
    interp0->base[0] = plane->affine.du;
    interp0->accum[1] = (xy & 0xFF00);
    interp0->base[1] = plane->affine.dv;
}
#endif

void __attribute__((optimize("O3"))) cgia_render(uint16_t y, uint32_t *rgbbuf)
{
    static union cgia_plane_regs_t *plane;
    static uint16_t *plane_offset;
    static struct cgia_plane_internal *plane_data;
    static uint16_t (*sprite_dscs)[CGIA_SPRITES];
    static uint8_t max_instr_count;

    CGIA.raster = y;
    int_mask |= CGIA_REG_INT_FLAG_RSI;
    if (y == 0)
        int_mask |= CGIA_REG_INT_FLAG_VBI;

    // track whether we need to fill line with background color
    // for transparent or sprite planes
    bool line_background_filled = false;

    // should trigger DLI after rasterizing the line?
    bool trigger_dli = false;

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

            plane = &CGIA.plane[p];
            plane_offset = &CGIA.offset[p];
            plane_data = &plane_int[p];
            sprite_dscs = &sprite_dsc_offsets[p];
            uint8_t *sprite_bank = vram_cache_ptr[1];

            if (vram_cache_bank_mask[1] != vram_wanted_bank_mask[1])
            {
                continue; // skip if the sprite bank is not synced yet
            }

            if (y == 0 // start of frame - reload descriptors
                || plane_data->sprites_need_update)
            {
                uint16_t offs = *plane_offset;
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

                plane_data->sprites_need_update = false;
            }

            // render sprites in reverse order
            // so lower indexed sprites have higher visual priority
            uint8_t sprite_index = 7;
            uint8_t mask = 0b10000000;

            // wait until back fill is done, as it may overwrite sprites on the right side
            dma_channel_wait_for_finish_blocking(back_chan);

            while (mask)
            {
                if (plane->sprite.active & mask)
                {
                    struct cgia_sprite_t *sprite = (struct cgia_sprite_t *)(sprite_bank + (*sprite_dscs)[sprite_index]);

                    int sprite_line = (sprite->flags & SPRITE_MASK_MIRROR_Y)
                                          ? sprite->pos_y + sprite->lines_y - 1 - y
                                          : y - sprite->pos_y;
                    if (sprite_line >= plane->sprite.start_y
                        && sprite_line < sprite->lines_y
                        && (!plane->sprite.stop_y || sprite_line <= plane->sprite.stop_y))
                    {
                        const uint8_t sprite_width = sprite->flags & SPRITE_MASK_WIDTH;
                        uint8_t line_bytes = sprite_width + 1;
                        const uint sprite_offset = sprite_line * line_bytes;

                        uint8_t *dst = sprite_line_data;
                        uint8_t *src = sprite_bank + sprite->data_offset;
                        if (sprite->flags & SPRITE_MASK_MIRROR_X) // TODO: inc/dec inside renderer
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
            // borders
            uint8_t border_columns = plane->sprite.border_columns;
            if (border_columns > MAX_BORDER_COLUMNS)
                border_columns = MAX_BORDER_COLUMNS;
            if (border_columns)
            {
                uint32_t *buf = fill_back(rgbbuf, border_columns, CGIA.back_color);
                uint8_t row_columns = FRAME_CHARS - 2 * border_columns;
                buf += row_columns * CGIA_COLUMN_PX;
                fill_back(buf, border_columns, CGIA.back_color);
            }
        }
        else
        {
            /* --- BACKGROUND --- */
            plane = &CGIA.plane[p];
            plane_offset = &CGIA.offset[p];
            plane_data = &plane_int[p];
            max_instr_count = CGIA_MAX_DL_INSTR_PER_LINE;

        restart_plane:
            if (y == 0) // start of frame - reset flags and counters
            {
                plane_data->wait_vbl = false;
                plane_data->row_line_count = 0;
            }

            if (!(CGIA.planes & (1u << p)))
                continue; // next if not enabled

        process_instruction:
            if (plane_data->wait_vbl) // DL is stopped and waiting for VBL
            {
                // if the plane border is not transparent, it should become
                // a filled background for other planes
                if (!(plane->bckgnd.flags & PLANE_MASK_BORDER_TRANSPARENT))
                {
                    // generate full-length border line
                    (void)fill_back(rgbbuf, DISPLAY_WIDTH_PIXELS / CGIA_COLUMN_PX, CGIA.back_color);
                    line_background_filled = true;
                }

                continue; // and we're done
            }

            uint8_t *bckgnd_bank = vram_cache_ptr[0];
            uint8_t dl_instr = bckgnd_bank[*plane_offset];
            uint8_t instr_code = dl_instr & 0b00001111;
            int_mask |= CGIA_REG_INT_FLAG_DLI;

            if (vram_cache_bank_mask[0] != vram_wanted_bank_mask[0])
            {
                continue; // skip if the bg bank is not synced yet
            }

            // Display list row takes a plane-regs defined raster lines,
            // or may be encoded in instruction itself (gets modified later)
            uint8_t dl_row_lines = plane->bckgnd.row_height;

            if (0 == max_instr_count--)
            {
                // move to next plane if we already processed maximum allowed instructions per raster line
                goto plane_epilogue;
            }

            // If the plane is transparent and we didn't render anything
            // to the framebuffer yet, we need to start background fill
            // so we see background color in the transparent "holes"
            if (!line_background_filled && plane->bckgnd.flags & PLANE_MASK_TRANSPARENT)
            {
                // fill the whole line with background color
                (void)fill_back(rgbbuf, DISPLAY_WIDTH_PIXELS / CGIA_COLUMN_PX, CGIA.back_color);
                line_background_filled = true;
            }

            // first process instructions - they need less preparation and can be shortcutted
            if (!(dl_instr & CGIA_DL_MODE_BIT))
            {
                switch (instr_code)
                {
                    // ------- Instructions --------------

                case 0x0: // INSTR0 - blank lines
                    dl_row_lines = dl_instr >> 4;
                    if (!line_background_filled || !(plane->bckgnd.flags & PLANE_MASK_BORDER_TRANSPARENT))
                    {
                        // fill the whole line with background color
                        (void)fill_back(rgbbuf, DISPLAY_WIDTH_PIXELS / CGIA_COLUMN_PX, CGIA.back_color);
                        // line_background_filled = true; // set in plane_epilogue
                    }
                    goto plane_epilogue;

                case 0x1: // INSTR1 - duplicate lines
                    dl_row_lines = dl_instr >> 4;
                    // FIXME: for now leave RGB buffer as is - will display it again
                    // TODO: store last RGB buffer pointer at the end of the frame and copy to current one
                    goto plane_epilogue;

                case 0x2: // INSTR1 - JMP
                    // Load DL address
                    *plane_offset = (uint16_t)((bckgnd_bank[++*plane_offset])
                                               | (bckgnd_bank[++*plane_offset] << 8));
                    plane_data->row_line_count = 0; // will start new row

                    if (dl_instr & CGIA_DL_DLI_BIT)
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
                        plane_data->memory_scan = (uint16_t)((bckgnd_bank[++*plane_offset])
                                                             | (bckgnd_bank[++*plane_offset] << 8));
                    }
                    if (dl_instr & 0b00100000)
                    {
                        plane_data->colour_scan = (uint16_t)((bckgnd_bank[++*plane_offset])
                                                             | (bckgnd_bank[++*plane_offset] << 8));
                    }
                    if (dl_instr & 0b01000000)
                    {
                        plane_data->backgr_scan = (uint16_t)((bckgnd_bank[++*plane_offset])
                                                             | (bckgnd_bank[++*plane_offset] << 8));
                    }
                    if (dl_instr & 0b10000000)
                    {
                        plane_data->char_gen_offset = (uint16_t)((bckgnd_bank[++*plane_offset])
                                                                 | (bckgnd_bank[++*plane_offset] << 8));
                    }
                    ++*plane_offset; // Move to next DL instruction
                    goto process_instruction;

                case 0x4: // Set 8-bit register
                {
                    plane->reg[(dl_instr & 0b11110000) >> 4] = bckgnd_bank[++*plane_offset];
                }
                    ++*plane_offset; // Move to next DL instruction
                    goto process_instruction;

                case 0x5: // Set 16-bit register
                {
                    uint8_t idx = (dl_instr & 0b01110000) >> 3;
                    plane->reg[idx++] = bckgnd_bank[++*plane_offset];
                    plane->reg[idx] = bckgnd_bank[++*plane_offset];
                }
                    ++*plane_offset; // Move to next DL instruction
                    goto process_instruction;

                // ------- UNKNOWN INSTRUCTION
                default:
                    (void)fill_back(rgbbuf, DISPLAY_WIDTH_PIXELS / CGIA_COLUMN_PX, UNHANDLED_DL_COLOR);
                    dl_row_lines = plane_data->row_line_count; // force moving to next DL instruction
                    goto plane_epilogue;
                }
            }
            else
            {
                uint8_t border_columns = plane->bckgnd.border_columns;
                if (border_columns > MAX_BORDER_COLUMNS)
                    border_columns = MAX_BORDER_COLUMNS;
                uint8_t row_columns = FRAME_CHARS - 2 * border_columns;

                if (row_columns)
                {
                    // ------- Mode Rows --------------
                    switch (instr_code)
                    {
                    case (0x2 | CGIA_DL_MODE_BIT): // MODE2 (A) - text/tile mode
                    {
                        set_linear_scans(1,
                                         bckgnd_bank + plane_data->memory_scan - 1,
                                         bckgnd_bank + plane_data->colour_scan - 1,
                                         bckgnd_bank + plane_data->backgr_scan - 1);
                        uint8_t char_shift = log2_tab[plane->bckgnd.row_height];
                        if (plane->bckgnd.flags & PLANE_MASK_TRANSPARENT)
                        {
                            cgia_encode_mode_2_shared(
                                rgbbuf + border_columns * CGIA_COLUMN_PX + plane->bckgnd.scroll_x,
                                row_columns,
                                bckgnd_bank + plane_data->char_gen_offset + plane_data->row_line_count,
                                char_shift);
                        }
                        else
                        {
                            cgia_encode_mode_2_mapped(
                                rgbbuf + border_columns * CGIA_COLUMN_PX + plane->bckgnd.scroll_x,
                                row_columns,
                                bckgnd_bank + plane_data->char_gen_offset + plane_data->row_line_count,
                                char_shift);
                        }
                    }
                    break;

                    case (0x3 | CGIA_DL_MODE_BIT): // MODE3 (B) - bitmap mode
                    {
                        const uint8_t row_height = plane->bckgnd.row_height + 1;
                        set_linear_scans(row_height,
                                         bckgnd_bank + plane_data->memory_scan - row_height,
                                         bckgnd_bank + plane_data->colour_scan - 1,
                                         bckgnd_bank + plane_data->backgr_scan - 1);
                        // TODO: double size
                        if (plane->bckgnd.flags & PLANE_MASK_TRANSPARENT)
                        {
                            cgia_encode_mode_3_shared(
                                rgbbuf + border_columns * CGIA_COLUMN_PX + plane->bckgnd.scroll_x,
                                row_columns);
                        }
                        else
                        {
                            cgia_encode_mode_3_mapped(
                                rgbbuf + border_columns * CGIA_COLUMN_PX + plane->bckgnd.scroll_x,
                                row_columns);
                        }

                        // next raster line starts with next byte, but color/bg scan stay the same
                        ++plane_data->memory_scan;
                    }
                    break;

                    case (0x4 | CGIA_DL_MODE_BIT): // MODE4 (C) - multicolor text/tile mode
                    {
                        set_linear_scans(1,
                                         bckgnd_bank + plane_data->memory_scan - 1,
                                         bckgnd_bank + plane_data->colour_scan - 1,
                                         bckgnd_bank + plane_data->backgr_scan - 1);
                        uint8_t char_shift = log2_tab[plane->bckgnd.row_height];
                        if (plane->bckgnd.flags & PLANE_MASK_TRANSPARENT)
                        {
                            if (plane->bckgnd.flags & PLANE_MASK_DOUBLE_WIDTH)
                                cgia_encode_mode_4_doubled_shared(
                                    rgbbuf + border_columns * CGIA_COLUMN_PX + plane->bckgnd.scroll_x,
                                    row_columns,
                                    bckgnd_bank + plane_data->char_gen_offset + plane_data->row_line_count,
                                    char_shift,
                                    plane->bckgnd.shared_color);
                            else
                                cgia_encode_mode_4_shared(
                                    rgbbuf + border_columns * CGIA_COLUMN_PX + plane->bckgnd.scroll_x,
                                    // this mode generates 4px columns, so requires 2x columns
                                    row_columns << 1,
                                    bckgnd_bank + plane_data->char_gen_offset + plane_data->row_line_count,
                                    char_shift,
                                    plane->bckgnd.shared_color);
                        }
                        else
                        {
                            if (plane->bckgnd.flags & PLANE_MASK_DOUBLE_WIDTH)
                                cgia_encode_mode_4_doubled_mapped(
                                    rgbbuf + border_columns * CGIA_COLUMN_PX + plane->bckgnd.scroll_x,
                                    row_columns,
                                    bckgnd_bank + plane_data->char_gen_offset + plane_data->row_line_count, char_shift,
                                    plane->bckgnd.shared_color);
                            else
                                cgia_encode_mode_4_mapped(
                                    rgbbuf + border_columns * CGIA_COLUMN_PX + plane->bckgnd.scroll_x,
                                    // this mode generates 4px columns, so requires 2x columns
                                    row_columns << 1,
                                    bckgnd_bank + plane_data->char_gen_offset + plane_data->row_line_count, char_shift,
                                    plane->bckgnd.shared_color);
                        }
                    }
                    break;

                    case (0x5 | CGIA_DL_MODE_BIT): // MODE5 (D) - multicolor bitmap mode
                    {
                        int offset_delta = plane->bckgnd.offset_x - 1;
                        const uint8_t *cs = bckgnd_bank + plane_data->colour_scan + offset_delta;
                        const uint8_t *bs = bckgnd_bank + plane_data->backgr_scan + offset_delta;
                        uint8_t row_height = plane->bckgnd.row_height;
                        offset_delta <<= log2_tab[row_height];
                        const uint8_t *ms = bckgnd_bank + plane_data->memory_scan + offset_delta;
                        set_linear_scans(++row_height, ms, cs, bs);

                        uint8_t encode_columns = row_columns;
                        if (plane->bckgnd.stride)
                        {
                            int8_t scr_delta = plane->bckgnd.scroll_x;
                            if (scr_delta < 0)
                                scr_delta -= 7;
                            encode_columns = (uint8_t)(encode_columns - scr_delta / CGIA_COLUMN_PX);
                        }
                        if (plane->bckgnd.flags & PLANE_MASK_TRANSPARENT)
                        {
                            if (plane->bckgnd.flags & PLANE_MASK_DOUBLE_WIDTH)
                            {
                                cgia_encode_mode_5_doubled_shared(
                                    rgbbuf + border_columns * CGIA_COLUMN_PX + plane->bckgnd.scroll_x,
                                    encode_columns,
                                    plane->bckgnd.shared_color);
                            }
                            else
                            {
                                // this mode generates 4x8 cells, so requires 2x columns
                                encode_columns <<= 1;
                                cgia_encode_mode_5_shared(
                                    rgbbuf + border_columns * CGIA_COLUMN_PX + plane->bckgnd.scroll_x,
                                    encode_columns,
                                    plane->bckgnd.shared_color);
                            }
                        }
                        else
                        {
                            if (plane->bckgnd.flags & PLANE_MASK_DOUBLE_WIDTH)
                            {
                                cgia_encode_mode_5_doubled_mapped(
                                    rgbbuf + border_columns * CGIA_COLUMN_PX + plane->bckgnd.scroll_x,
                                    encode_columns,
                                    plane->bckgnd.shared_color);
                            }
                            else
                            {
                                // this mode generates 4x8 cells, so requires 2x columns
                                encode_columns <<= 1;
                                cgia_encode_mode_5_mapped(
                                    rgbbuf + border_columns * CGIA_COLUMN_PX + plane->bckgnd.scroll_x,
                                    encode_columns,
                                    plane->bckgnd.shared_color);
                            }
                        }

                        // next raster line starts with next byte, but color/bg scan stay the same
                        ++plane_data->memory_scan;
                    }
                    break;

                    case (0x6 | CGIA_DL_MODE_BIT): // MODE6 (E) - HAM mode
                    {
                        set_linear_scans(1,
                                         bckgnd_bank + plane_data->memory_scan - 1,
                                         bckgnd_bank, bckgnd_bank);
                        if (plane->bckgnd.flags & PLANE_MASK_DOUBLE_WIDTH)
                        {
                            cgia_encode_mode_6_doubled(
                                rgbbuf + border_columns * CGIA_COLUMN_PX + plane->bckgnd.scroll_x,
                                row_columns,
                                plane->ham.base_color,
                                CGIA.back_color);
                        }
                        else
                        {
                            cgia_encode_mode_6(
                                rgbbuf + border_columns * CGIA_COLUMN_PX + plane->bckgnd.scroll_x,
                                // this mode generates 4px columns, so requires 2x columns
                                row_columns << 1,
                                plane->ham.base_color,
                                CGIA.back_color);
                        }
                        // next raster line starts with next byte
                        ++plane_data->memory_scan;
                    }
                    break;

                    case (0x7 | CGIA_DL_MODE_BIT): // MODE7 (F) - affine transform mode
                    {
                        if (plane_data->row_line_count == 0)
                        {
                            // start interpolators
                            set_mode7_interp_config(plane);
                        }
                        else
                        {
                            // restore interpolators state for this plane
                            interp_restore(interp0, &plane_data->interpolator[0]);
                            interp_restore(interp1, &plane_data->interpolator[1]);
                        }
                        set_mode7_scans(plane, bckgnd_bank + plane_data->memory_scan);

                        cgia_encode_mode_7(
                            rgbbuf + border_columns * CGIA_COLUMN_PX,
                            row_columns);

                        // save interpolators state for this plane
                        interp_save(interp0, &plane_data->interpolator[0]);
                        interp_save(interp1, &plane_data->interpolator[1]);
                    }
                    break;

                    // ------- UNKNOWN MODE - generate pink line (should not happen)
                    default:
                        (void)fill_back(rgbbuf, DISPLAY_WIDTH_PIXELS / CGIA_COLUMN_PX, UNHANDLED_DL_COLOR);
                        dl_row_lines = plane_data->row_line_count; // force moving to next DL instruction
                        goto plane_epilogue;
                    }
                }

                // borders
                if ((dl_instr & CGIA_DL_MODE_BIT) && border_columns && !(plane->bckgnd.flags & PLANE_MASK_BORDER_TRANSPARENT))
                {
                    uint32_t *buf = fill_back(rgbbuf, border_columns, CGIA.back_color);
                    buf += row_columns * CGIA_COLUMN_PX;
                    fill_back(buf, border_columns, CGIA.back_color);
                }
            }

        plane_epilogue:
            // this line has drawn something, so next planes don't have to fill background
            line_background_filled = true;

            // should trigger DLI?
            if (dl_instr & CGIA_DL_DLI_BIT)
            {
                trigger_dli = true;
            }

            // Should we run a new DL row?
            if (plane_data->row_line_count == dl_row_lines)
            {
                // Update scan pointers
                if (dl_instr & CGIA_DL_MODE_BIT && instr_code != (0x7 | CGIA_DL_MODE_BIT))
                {
                    // update scan pointers to next value
                    uint8_t stride = plane->bckgnd.stride;
                    if (stride)
                    {
                        plane_data->colour_scan += stride;
                        plane_data->backgr_scan += stride;
                        plane_data->memory_scan += --stride * (plane->bckgnd.row_height + 1);
                    }
                    else
                    {
                        plane_data->memory_scan = (uint16_t)((uint8_t *)(interp_get_accumulator(interp0, 0) + 1) - bckgnd_bank);
                        plane_data->colour_scan = (uint16_t)((uint8_t *)(interp_get_accumulator(interp1, 0) + 1) - bckgnd_bank);
                        plane_data->backgr_scan = (uint16_t)((uint8_t *)(interp_get_accumulator(interp1, 1) + 1) - bckgnd_bank);
                    }
                }

                // Reset line counter
                plane_data->row_line_count = 0;

                // Move to next DL instruction
                ++*plane_offset;
            }
            else
            {
                // Move to next line of current DL row
                ++plane_data->row_line_count;
            }
        }
    }

    // if we ended-up here without painting the line, we need to fill it with back color
    if (!line_background_filled)
    {
        // generate full-length border line
        (void)fill_back(rgbbuf, DISPLAY_WIDTH_PIXELS / CGIA_COLUMN_PX, CGIA.back_color);
        line_background_filled = true;
    }

    // bump right after processing, so CPU is free to modify regs
    // before next line rasterization starts
    ++CGIA.raster;
    if (CGIA.raster >= DISPLAY_HEIGHT_LINES)
        CGIA.raster = 0;

    // trigger raster-line interrupt
    if ((CGIA.int_enable & CGIA_REG_INT_FLAG_RSI) && (CGIA.raster == CGIA.int_raster))
    {
        CGIA.int_status |= CGIA_REG_INT_FLAG_RSI;
    }
    if ((CGIA.int_enable & CGIA_REG_INT_FLAG_DLI) && trigger_dli)
    {
        CGIA.int_status |= CGIA_REG_INT_FLAG_DLI;
    }

    cpu_set_nmi();
}

static void _cgia_transfer_vcache_bank(uint8_t bank);

void cgia_task(void)
{
    cpu_set_nmi();

    _cgia_transfer_vcache_bank(0);
    _cgia_transfer_vcache_bank(1);
}

#ifdef PICO_SDK_VERSION_MAJOR
static void _cgia_transfer_vcache_bank(uint8_t bank)
{
    if (vram_wanted_bank_mask[bank] != vram_cache_bank_mask[bank])
    {
        // TODO: start DMA transfer from PSRAM to VRAM
        // - store bank id and destination being trasferred
        // - do not start if transfer already in progress
        // - allocate PSRAM chip, so CPU gets blocked until transfer is done
        // - update vram_cache_bank_mask when transfer is done with stored value
        //   - vram_wanted_bank_mask might already have changed and next transfer
        //   - will be started next tick
    }
}
#endif
