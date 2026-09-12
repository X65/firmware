#pragma once

#ifndef __ASSEMBLER__
#include <string.h>
#include <sys/types.h>
#endif

#define CGIA_COLUMN_PX (8)

/**
    bit 3 unset (0-7) - instructions:
    Bits 0-2 encode the instruction:
    0 - empty lines filled with fill color
        bits 6-4 - how many
        bit 7 - DLI
    1 - [TBD]
    2 - JMP Display List (Load DL offset address)
        - DLI bit set - wait for Vertical Blank
    3 - Load Memory - bits 4-7 flag which offsets will follow
        4 - LMS - memory scan
        5 - LFS - color scan
        6 - LBS - background scan
        7 - LCG - character generator address
    4 - Load 8 bit value to Register Offset
    5 - Load 16 bit value to Register Offset
        bits 6-4 - register index
    6 - [TBD]
    7 - [TBD]

    bit 3 set (8-F) - generate mode row:
    Bits 0-2 encode the mode:
    0 - palette text/tile mode
    1 - palette bitmap mode
    2 - attribute text/tile mode
    3 - attribute bitmap mode
    4 - [TBD]
    5 - [TBD]
    6 - Hold-and-Modify (HAM) mode
    7 - affine transform chunky pixel mode

    bit 7 - trigger DLI - Display List Interrupt
*/

#define CGIA_DL_MODE_BIT         0b00001000
#define CGIA_DL_DOUBLE_WIDTH_BIT 0b00010000
#define CGIA_DL_MULTICOLOR_BIT   0b00100000
#define CGIA_DL_RESERVED_BIT     0b01000000
#define CGIA_DL_DLI_BIT          0b10000000

// Multicolor encoding
// https://csbruce.com/cbm/hacking/hacking12.txt
/*
   00   background color
   01   same as "off" color in hires mode
   10   same as "on" color in hires mode
   11   another "background" color
*/

/*
   Hold-And-Modify MODE6

   HAM commands are 6bit each, 4 screen pixels packed in 3 bytes.
   [CCCDDD] - C -command bit, D - data bit

    000 - load base color index at DDD (one of 8 base colors)
    001 - blend current color with color at DDD

    CCS - CC:
          01 - Modify Red channel
          10 - Modify Green channel
          11 - Modify Blue channel

          S: sign, 0 +delta, 1 -delta
          DDD: delta (2's complement, with above sign bit)
*/

#define CGIA_PLANE_REGS_NO (16)

#ifndef __ASSEMBLER__
union cgia_plane_regs_t
{
    struct cgia_bckgnd_regs
    {
        uint8_t flags;
        uint8_t border_columns;
        uint8_t row_height;
        uint8_t stride;
        int8_t scroll_x;
        int8_t offset_x;
        int8_t scroll_y;
        int8_t offset_y;
        uint8_t color[8];
    } bckgnd;

    struct cgia_ham_regs
    {
        uint8_t flags;
        uint8_t border_columns;
        uint8_t row_height;
        uint8_t reserved[5];
        uint8_t color[8];
    } ham;

    struct cgia_affine_regs
    {
        uint8_t flags;
        uint8_t border_columns;
        uint8_t row_height;
        uint8_t texture_bits; // 2-0 width_bits-1, 6-4 height_bits-1; 0..7 => 2..256 px
        int16_t u;
        int16_t v;
        int16_t du;
        int16_t dv;
        int16_t dx;
        int16_t dy;
    } affine;

    struct cgia_sprite_regs
    {
        uint8_t active; // bitmask for active sprites
        uint8_t border_columns;
        uint8_t start_y;
        uint8_t stop_y;
        uint8_t reserved[4];
        uint8_t color[8]; // shared by every sprite on the plane (palette entries 4..11)
    } sprite;

    uint8_t reg[CGIA_PLANE_REGS_NO];
};

#define CGIA_PLANES                 (4)
#define CGIA_AFFINE_FRACTIONAL_BITS (8)
#define CGIA_MAX_DL_INSTR_PER_LINE  (32)

// plane flags:
// 0 - color 0 is transparent
// 1-2 - [RESERVED]
// 3 - border is transparent
// 4 - double-width pixel
// 5 - multicolor-pixel
// 6,7 - pixel bits: 00 - 1bit, 2 colors; 01 - 2bit, 4 colors;
//                   10 - 3bit, 8 colors; 11 - 4bit, 8 colors + half-bright
#define PLANE_MASK_TRANSPARENT        0b00000001
#define PLANE_MASK_BORDER_TRANSPARENT 0b00001000
#define PLANE_MASK_DOUBLE_WIDTH       0b00010000
#define PLANE_MASK_MULTICOLOR         0b00100000
#define PLANE_MASK_PIXEL_BITS         0b11000000

#define PLANE_MASK_FROM_DL (PLANE_MASK_DOUBLE_WIDTH | PLANE_MASK_MULTICOLOR)

#define PLANE_BITS_1BPP (0b00 << 6)
#define PLANE_BITS_2BPP (0b01 << 6)
#define PLANE_BITS_3BPP (0b10 << 6)
#define PLANE_BITS_4BPP (0b11 << 6)

struct cgia_t
{
    uint8_t mode;

    uint8_t bckgnd_bank;
    uint8_t sprite_bank;
    uint8_t _ctl_reserved[16 - 3];
    // -------------------------------------------------------------------
    uint16_t raster;
    uint8_t _rst_reserved1[8 - 2];

    uint16_t int_raster; // Line to generate raster interrupt.
    uint8_t int_enable;  // Interrupt flags. [VBI DLI RSI x x x x x]
    uint8_t int_status;  // Interrupt flags. [VBI DLI RSI x x x x x]
    uint8_t _rst_reserved2[8 - 4];
    // -------------------------------------------------------------------
    uint8_t _reserved[16];
    // -------------------------------------------------------------------
    uint8_t planes; // [TTTTEEEE] EEEE - enable bits, TTTT - type (0 bckgnd, 1 sprite)
    uint8_t order;  // plane order permutation - SJT ordering
    uint8_t _pln_reserved1[4 - 2];

    uint8_t back_color;
    uint8_t _pln_reserved[4 - 1];

    uint16_t offset[CGIA_PLANES]; // DisplayList or SpriteDescriptor table start
    // -------------------------------------------------------------------
    union cgia_plane_regs_t plane[CGIA_PLANES];
};

#define CGIA_MODE_HIRES_BIT     0b00000001 // 96 columns (768px horz) mode
#define CGIA_MODE_INTERLACE_BIT 0b00000010 // interlace (480px vert) mode

// register indices
#define CGIA_REG_MODE        (offsetof(struct cgia_t, mode))
#define CGIA_REG_BCKGND_BANK (offsetof(struct cgia_t, bckgnd_bank))
#define CGIA_REG_SPRITE_BANK (offsetof(struct cgia_t, sprite_bank))
#define CGIA_REG_RASTER      (offsetof(struct cgia_t, raster))
#define CGIA_REG_INT_RASTER  (offsetof(struct cgia_t, int_raster))
#define CGIA_REG_INT_ENABLE  (offsetof(struct cgia_t, int_enable))
#define CGIA_REG_INT_STATUS  (offsetof(struct cgia_t, int_status))
#define CGIA_REG_PLANES      (offsetof(struct cgia_t, planes))
#define CGIA_REG_BACK_COLOR  (offsetof(struct cgia_t, back_color))

#define CGIA_REG_INT_FLAG_VBI 0b10000000
#define CGIA_REG_INT_FLAG_DLI 0b01000000
#define CGIA_REG_INT_FLAG_RSI 0b00100000

struct cgia_sprite_t
{
    // --- SPRITE DESCRIPTOR --- (16 bytes) ---
    int16_t pos_x;
    int16_t pos_y;
    uint16_t lines_y;
    uint8_t flags;
    uint8_t reserved_f;
    uint8_t color[4];
    uint16_t data_offset;
    uint16_t next_dsc_offset; // after passing lines_y, reload sprite descriptor data
                              // this is a built-in sprite multiplexer
};
#endif // __ASSEMBLER__

#define CGIA_SPRITES     (8)
#define SPRITE_MAX_WIDTH (8)

// sprite flags:
// 0-2 - width in 8 pixel columns, minus one (1..8 columns, 8..64 px)
// 3 - double-width
// 4-5 - pixel bits: 00 - 1bit, 01 - 2bit, 10 - 3bit, 11 - 4bit
//       (same encoding as the plane's PLANE_MASK_PIXEL_BITS)
// 6 - mirror X
// 7 - mirror Y
#define SPRITE_MASK_WIDTH        0b00000111
#define SPRITE_MASK_DOUBLE_WIDTH 0b00001000
#define SPRITE_MASK_PIXEL_BITS   0b00110000
#define SPRITE_MASK_MIRROR_X     0b01000000
#define SPRITE_MASK_MIRROR_Y     0b10000000

#define SPRITE_PIXEL_BITS_SHIFT 4
#define SPRITE_BITS_1BPP        (0b00 << SPRITE_PIXEL_BITS_SHIFT)
#define SPRITE_BITS_2BPP        (0b01 << SPRITE_PIXEL_BITS_SHIFT)
#define SPRITE_BITS_3BPP        (0b10 << SPRITE_PIXEL_BITS_SHIFT)
#define SPRITE_BITS_4BPP        (0b11 << SPRITE_PIXEL_BITS_SHIFT)
// the old name for 2bpp sprites
#define SPRITE_MASK_MULTICOLOR  SPRITE_BITS_2BPP

/*
    Sprite pixel data uses the MODE1 packing at every depth: a column of
    8 pixels takes `bpp` bytes, most significant pixel first, so a line is
    `bpp * columns` bytes long.

    Pixel values index one 16 entry palette per sprite, and a deeper sprite
    simply reaches further into it:

      bits 3:2 | bits 1:0 | draws
      ---------+----------+--------------------------------------------
        00     |   cc     | descriptor color[cc]     (0000 is transparent)
        01     |   cc     | plane color[cc]
        10     |   cc     | plane color[4+cc]
        11     |   cc     | descriptor color[cc], half-bright (index ^ 4)

    1bpp sees entry 1, 2bpp entries 1..3, 3bpp entries 1..7 (bit 3 dropped),
    4bpp all of them. Entry 0 is always transparent, so descriptor color[0]
    is only ever drawn through entry 12, half-bright.
*/

// a palette index is hue * 8 + level; toggling level bit 2 moves four levels
#define CGIA_COLOR_HALF_BRIGHT 0b00000100

#ifndef __ASSEMBLER__
// bits per pixel of a sprite, 1..4
static inline uint sprite_bpp(uint8_t flags)
{
    return 1 + ((flags & SPRITE_MASK_PIXEL_BITS) >> SPRITE_PIXEL_BITS_SHIFT);
}

// The 16 entry palette above, in two parts: the plane part is the same for
// every sprite on the plane, the descriptor part differs per sprite.
static inline void sprite_palette_plane(const uint8_t plane_colors[8], uint8_t palette[16])
{
    memcpy(palette + 4, plane_colors, 8);
}
static inline void sprite_palette_descriptor(const uint8_t colors[4], uint8_t palette[16])
{
    memcpy(palette, colors, 4);
    for (int i = 0; i < 4; ++i)
        palette[12 + i] = colors[i] ^ CGIA_COLOR_HALF_BRIGHT;
}
// the palette of the sprite being encoded; the renderer fills it, the encoders read it
extern uint8_t sprite_colors[16];

// ---- internals ----
void cgia_init(void);
void cgia_reset(void);
void cgia_render(uint16_t y, uint32_t *rgbbuf);
void cgia_vbi(void);
uint8_t cgia_reg_read(uint8_t reg_no);
void cgia_reg_write(uint8_t reg_no, uint8_t value);

void cgia_task(void);

#define CGIA_VRAM_BANKS (2)
extern uint8_t vram_cache[CGIA_VRAM_BANKS][0x10000];
// pass EVERY RAM write through CGIA for updating VRAM cache banks
void cgia_ram_write(uint8_t bank, uint16_t addr, uint8_t data);
// VCACHE DMA transfer control
extern uint8_t vcache_dma_bank;
extern uint16_t vcache_dma_blocks_remaining;
extern uint8_t *vcache_dma_dest;
#endif // __ASSEMBLER__
