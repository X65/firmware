#include "sys/types.h"

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
    0 - HighRes text (80/96 columns) mode
    1 - HighRes bitmap mode, 256 colors
    2 - text/tile mode
    3 - bitmap mode
    4 - multicolor text/tile mode
    5 - multicolor bitmap mode
    6 - doubled multicolor text/tile mode
    7 - doubled multicolor bitmap mode

    bit 7 - trigger DLI - Display List Interrupt
*/

// https://csbruce.com/cbm/hacking/hacking12.txt
/*
   00   background color
   01   same as "off" color in hires mode
   10   same as "on" color in hires mode
   11   another "background" color
*/

#define CGIA_COLUMN_PX 8

struct cgia_plane_t
{
    uint16_t offset; // Current DisplayList or SpriteDescriptor table start

    union cgia_plane_regs
    {
        struct cgia_bckgnd_regs
        {
            uint8_t flags;
            uint8_t shared_color[2];
            uint8_t border_columns;
            uint8_t row_height;
            int8_t scroll;
            int8_t offset;
            uint8_t stride;
        } bckgnd;

        struct cgia_sprite_regs
        {
            uint8_t count;
        } sprite;
    } regs;
};

#define CGIA_PLANES 4

// plane flags:
// 0 - color 0 is transparent
// 1-7 - [RESERVED]
#define PLANE_MASK_TRANSPARENT 0b00000001

struct cgia_t
{
    uint8_t planes; // [TTTTEEEE] EEEE - enable bits, TTTT - type (0 bckgnd, 1 sprite)

    uint8_t bckgnd_bank;
    uint8_t sprite_bank;

    uint8_t back_color;

    struct cgia_plane_t plane[CGIA_PLANES];
};

extern struct cgia_t CGIA;

struct cgia_sprite_t
{
    // --- SPRITE DESCRIPTOR --- (16 bytes) ---
    int16_t pos_x;
    int16_t pos_y;
    uint16_t lines_y;
    uint8_t flags;
    uint8_t color[3];
    uint8_t reserved[2];
    uint16_t data_offset;
    uint16_t next_dsc_offset; // after passing lines_y, reload sprite descriptor data
                              // this is a built-in sprite multiplexer
};

// sprite flags:
// 0-1 - width in bytes
// 2 - [RESERVED]
// 3 - multicolor
// 4 - double-width
// 5 - mirror X
// 6 - mirror Y
// 7 - active
#define SPRITE_MASK_WIDTH        0b00000011
#define SPRITE_MASK_MULTICOLOR   0b00001000
#define SPRITE_MASK_DOUBLE_WIDTH 0b00010000
#define SPRITE_MASK_MIRROR_X     0b00100000
#define SPRITE_MASK_MIRROR_Y     0b01000000
#define SPRITE_MASK_ACTIVE       0b10000000

#define SPRITE_MAX_WIDTH 4

void cgia_init(void);
void cgia_core1_init(void);
void cgia_render(uint y, uint32_t *rgbbuf, uint8_t recursion_depth);
void cgia_vbl(void);
