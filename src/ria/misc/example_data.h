#include "sys/types.h"

#define split16(addr) (addr & 0xFF), ((addr >> 8) & 0xFF)

static const uint16_t text_mode_fg_color = 150; // 0x96
static const uint16_t text_mode_bg_color = 145; // 0x91
static const char *text_mode_hello = "              Hello World!              ";
static const uint16_t text_mode_video_offset = 0x0000;
static const uint16_t text_mode_color_offset = 0x1000;
static const uint16_t text_mode_bkgnd_offset = 0x2000;
static const uint16_t text_mode_chrgn_offset = 0x3000;
static const uint16_t text_mode_dl_offset = 0x3800;
static const uint8_t __attribute__((aligned(4))) text_mode_dl[] = {
    0x70, 0x70, 0x30,                                     // 2x 8 + 1x 4 of empty background lines
    0xF3, 0x00, 0x00, 0x00, 0x10, 0x00, 0x20, 0x00, 0x30, // LMS + LFS + LBS + LCG
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,       // 8x MODE2
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,       // 8x MODE2
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,       // 8x MODE2
    0x0A,                                                 // 1x MODE2
    0x82, split16(text_mode_dl_offset)                    // JMP to begin of DL and wait for Vertical BLank
};
static const uint8_t __attribute__((aligned(4))) text80_mode_dl[] = {
    0xF3,                                           // LMS + LFS + LBS + LCG
    split16(text_mode_video_offset),                // LMS
    split16(text_mode_color_offset),                // LFS
    split16(text_mode_bkgnd_offset),                // LBS
    split16(text_mode_chrgn_offset),                // LCG
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, // 8x MODE0
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, // 8x MODE0
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, // 8x MODE0
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08,             // 6x MODE0
    0x82, split16(text_mode_dl_offset)              // JMP to begin of DL and wait for Vertical BLank
};

#define EXAMPLE_SPRITE_WIDTH   4
#define EXAMPLE_SPRITE_HEIGHT  26
#define EXAMPLE_SPRITE_COLOR_1 0
#define EXAMPLE_SPRITE_COLOR_2 23
#define EXAMPLE_SPRITE_COLOR_3 10
uint8_t __attribute__((aligned(4))) example_sprite_data[EXAMPLE_SPRITE_WIDTH * EXAMPLE_SPRITE_HEIGHT] = {
    0b00000000, 0b00010101, 0b01000000, 0b00000000, //
    0b00000000, 0b01011111, 0b11010100, 0b00000000, //
    0b00000000, 0b01111111, 0b11111101, 0b00000000, //
    0b00000001, 0b01010101, 0b01111111, 0b01000000, //
    0b00000101, 0b01010101, 0b01011111, 0b11010000, //
    0b00000101, 0b10101010, 0b10100101, 0b11010000, //
    0b00000001, 0b10011001, 0b10101001, 0b01010100, //
    0b00000001, 0b10011001, 0b10101001, 0b01100100, //
    0b00000110, 0b10101010, 0b10100101, 0b01101001, //
    0b00000110, 0b10101010, 0b01101001, 0b10101001, //
    0b00010101, 0b10100101, 0b01011010, 0b10100100, //
    0b00000001, 0b01010101, 0b10101010, 0b01010000, //
    0b00000000, 0b01101010, 0b10100101, 0b01000000, //
    0b00010100, 0b01010101, 0b01011111, 0b11010000, //
    0b01101001, 0b01111101, 0b01111111, 0b11110100, //
    0b01100101, 0b11111101, 0b11010101, 0b11111101, //
    0b01100111, 0b11110101, 0b01101010, 0b01111101, //
    0b00010111, 0b11010101, 0b10101010, 0b10011101, //
    0b00011001, 0b01101001, 0b10101010, 0b10010100, //
    0b01111101, 0b01101001, 0b01101010, 0b01010000, //
    0b01111111, 0b01010101, 0b01010101, 0b11110100, //
    0b01111111, 0b01010101, 0b01010101, 0b01110101, //
    0b01111111, 0b01010101, 0b01010101, 0b01111101, //
    0b01011111, 0b01000000, 0b01010101, 0b01111101, //
    0b00010101, 0b00000000, 0b00000001, 0b01111101, //
    0b00000000, 0b00000000, 0b00000000, 0b00010100, //
};

static const uint16_t affine_mode_video_offset = 0x0000;
static const uint16_t affine_mode_dl_offset = 0xF000;
static const uint8_t __attribute__((aligned(4))) affine_mode_dl[] = {
    0x13, 0x00, 0x00,                    // LMS
    0x0F, 0x0F,                          // 2x MODE7
    0x82, split16(affine_mode_dl_offset) // JMP to begin of DL and wait for Vertical BLank
};
static const uint16_t mixed_video_offset = 0xEA00;
static const uint16_t mixed_color_offset = 0xEC00;
static const uint16_t mixed_bkgnd_offset = 0xEE00;
static const uint16_t mixed_chrgn_offset = 0xE200;
static const uint16_t mixed_mode_dl_offset = 0xF000;
static const uint8_t __attribute__((aligned(4))) mixed_mode_dl[] = {
    0x13, 0x00, 0x00,                               // LMS
    0x14, 0x00,                                     // border_columns = 0
    0x24, 0x9f,                                     // row_height = 159
    0x34, (7 << 4) | 7,                             // texture_bits = 7,7
    0x0F,                                           // MODE7
    0x70,                                           // 8x empty lines
    0xF3,                                           // LMS + LFS + LBS + LCG
    split16(mixed_video_offset),                    // LMS
    split16(mixed_color_offset),                    // LFS
    split16(mixed_bkgnd_offset),                    // LBS
    split16(mixed_chrgn_offset),                    // LCG
    0x14, 0x04,                                     // border_columns = 4
    0x24, 0x07,                                     // row_height = 7
    0x34, 0x00,                                     // stride = 0
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, // 8x MODE2
    0x82, split16(mixed_mode_dl_offset)             // JMP to begin of DL and wait for Vertical BLank
};
