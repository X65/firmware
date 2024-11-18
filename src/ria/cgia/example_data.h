#include "sys/types.h"

static uint16_t text_mode_video_offset = 0x0000;
static uint16_t text_mode_color_offset = 0x1000;
static uint16_t text_mode_bkgnd_offset = 0x2000;
static uint16_t text_mode_chrgn_offset = 0x3000;
static uint16_t text_mode_dl_offset = 0x3800;

static uint8_t __attribute__((aligned(4))) text_mode_dl[] = {
    0x70, 0x70, 0x30,                                     // 2x 8 + 1x 4 of empty background lines
    0xF3, 0x00, 0x00, 0x00, 0x10, 0x00, 0x20, 0x00, 0x30, // LMS + LFS + LBS + LCG
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,       // 8x MODE2
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,       // 8x MODE2
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,       // 8x MODE2
    0x0A,                                                 // 1x MODE2
    0x82, 0x00, 0x38                                      // JMP to begin of DL and wait for Vertical BLank
};

static uint8_t __attribute__((aligned(4))) hires_mode_dl[] = {
    0x70, 0x70, 0x30,                         // 2x 8 + 1x 4 of empty background lines
    0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // LMS + LFS + LBS
    0x0B, 0x0B, 0x0B, 0x0B,                   // 4x MODE3
    0x0B, 0x0B, 0x0B, 0x0B,                   // 4x MODE3
    0x0B, 0x0B, 0x0B, 0x0B,                   // 4x MODE3
    0x0B, 0x0B, 0x0B, 0x0B,                   // 4x MODE3
    0x0B, 0x0B, 0x0B, 0x0B,                   // 4x MODE3
    0x0B, 0x0B, 0x0B, 0x0B, 0x0B,             // 5x MODE3 => 25 MODE3 lines
    0x82, 0x00, 0x00                          // JMP to begin of DL and wait for Vertical BLank
};

static uint8_t __attribute__((aligned(4))) multi_mode_dl[] = {
    0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,       // LMS + LFS + LBS
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, // 8x MODE7 => 30*8 = 240 lines of MODE7
    0x82, 0x00, 0x00                                // JMP to begin of DL and wait for Vertical BLank
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
