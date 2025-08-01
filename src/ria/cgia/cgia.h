#pragma once

#include "sys/types.h"

#define CGIA_COLUMN_PX (8)

/**
    bit 3 unset (0-7) - instructions:
    Bits 0-2 encode the instruction:
    0 - empty lines filled with fill color
        bits 6-4 - how many
        bit 7 - DLI
    1 - duplicate lines - copy last raster line n-times
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
    ...

    bit 3 set (8-F) - generate mode row:
    Bits 0-2 encode the mode:
    0 - [TBD]
    1 - [TBD]
    2 - text/tile mode
    3 - bitmap mode
    4 - multicolor text/tile mode
    5 - multicolor bitmap mode
    6 - Hold-and-Modify (HAM) mode
    7 - affine transform chunky pixel mode

    bit 7 - trigger DLI - Display List Interrupt
*/

#define CGIA_DL_MODE_BIT 0b00001000
#define CGIA_DL_DLI_BIT  0b10000000

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
          DDD: delta (0 offsetted, so 000 means 1)
*/

#define CGIA_PLANE_REGS_NO (16)

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
        uint8_t shared_color[2];
        uint8_t reserved[6];
    } bckgnd;

    struct cgia_ham_regs
    {
        uint8_t flags;
        uint8_t border_columns;
        uint8_t row_height;
        uint8_t reserved[5];
        uint8_t base_color[8];
    } ham;

    struct cgia_affine_regs
    {
        uint8_t flags;
        uint8_t border_columns;
        uint8_t row_height;
        uint8_t texture_bits; // 2-0 texture_width_bits, 6-4 texture_height_bits
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
// 5-7 - [RESERVED]
#define PLANE_MASK_TRANSPARENT        0b00000001
#define PLANE_MASK_BORDER_TRANSPARENT 0b00001000
#define PLANE_MASK_DOUBLE_WIDTH       0b00010000

struct cgia_pwm_t
{
    uint16_t freq;
    uint8_t duty;
    uint8_t _reserved;
};

#define CGIA_PWMS (2)

struct cgia_t
{
    uint8_t mode;

    uint8_t bckgnd_bank;
    uint8_t sprite_bank;
    uint8_t _reserved[16 - 3];

    uint16_t raster;
    uint8_t _raster_res1[6];
    uint16_t int_raster; // Line to generate raster interrupt.
    uint8_t int_enable;  // Interrupt flags. [VBI DLI RSI x x x x x]
    uint8_t int_status;  // Interrupt flags. [VBI DLI RSI x x x x x]
    uint8_t _raster_res2[4];

    struct cgia_pwm_t pwm[CGIA_PWMS];
    struct cgia_pwm_t _reserved_pwm[4 - CGIA_PWMS];

    uint8_t planes; // [TTTTEEEE] EEEE - enable bits, TTTT - type (0 bckgnd, 1 sprite)
    uint8_t back_color;
    uint8_t _reserved_planes[8 - 2];
    uint16_t offset[CGIA_PLANES]; // DisplayList or SpriteDescriptor table start
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
#define CGIA_REG_PWM_0_FREQ  (0x20) // PWM channel 0 frequency.
#define CGIA_REG_PWM_0_DUTY  (0x22) // PWM channel 0 duty-cycle.
#define CGIA_REG_PWM_1_FREQ  (0x24) // PWM channel 1 frequency.
#define CGIA_REG_PWM_1_DUTY  (0x26) // PWM channel 1 duty-cycle.

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
    uint8_t color[3];
    uint8_t reserved_c;
    uint16_t data_offset;
    uint16_t next_dsc_offset; // after passing lines_y, reload sprite descriptor data
                              // this is a built-in sprite multiplexer
};

#define CGIA_SPRITES     (8)
#define SPRITE_MAX_WIDTH (8)

// sprite flags:
// 0-2 - width in bytes
// 3 - multicolor
// 4 - double-width
// 5 - mirror X
// 6 - mirror Y
// 7 - [RESERVED]
#define SPRITE_MASK_WIDTH        0b00000111
#define SPRITE_MASK_MULTICOLOR   0b00001000
#define SPRITE_MASK_DOUBLE_WIDTH 0b00010000
#define SPRITE_MASK_MIRROR_X     0b00100000
#define SPRITE_MASK_MIRROR_Y     0b01000000

// ---- internals ----
void cgia_init(void);
void cgia_render(uint16_t y, uint32_t *rgbbuf);
void cgia_vbi(void);
uint8_t cgia_reg_read(uint8_t reg_no);
void cgia_reg_write(uint8_t reg_no, uint8_t value);

void cgia_task(void);

#define CGIA_VRAM_BANKS (2)
// pass EVERY RAM write through CGIA for updating VRAM cache banks
void cgia_ram_write(uint32_t addr, uint8_t data);
