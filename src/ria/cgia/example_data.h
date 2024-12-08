#include "sys/types.h"

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
    0x82, 0x00, 0x38                                      // JMP to begin of DL and wait for Vertical BLank
};
static const uint8_t __attribute__((aligned(4))) text80_mode_dl[] = {
    0xF3,                                                                    // LMS + LFS + LBS + LCG
    (text_mode_video_offset & 0xFF), ((text_mode_video_offset >> 8) & 0xFF), // LMS
    (text_mode_color_offset & 0xFF), ((text_mode_color_offset >> 8) & 0xFF), // LFS
    (text_mode_bkgnd_offset & 0xFF), ((text_mode_bkgnd_offset >> 8) & 0xFF), // LBS
    (text_mode_chrgn_offset & 0xFF), ((text_mode_chrgn_offset >> 8) & 0xFF), // LCG
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,                          // 8x MODE0
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,                          // 8x MODE0
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,                          // 8x MODE0
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08,                                      // 6x MODE0
    0x82, 0x00, 0x38                                                         // JMP to begin of DL and wait for Vertical BLank
};

static const uint16_t hires_mode_video_offset = 0x4000;
static const uint16_t hires_mode_color_offset = 0x6000;
static const uint16_t hires_mode_bkgnd_offset = 0x6800;
static const uint16_t hires_mode_dl_offset = 0x7000;
static const uint8_t __attribute__((aligned(4))) hires_mode_dl[] = {
    0x70, 0x70, 0x30,                         // 2x 8 + 1x 4 of empty background lines
    0x73, 0x00, 0x40, 0x00, 0x60, 0x00, 0x68, // LMS + LFS + LBS
    0x0B, 0x0B, 0x0B, 0x0B,                   // 4x MODE3
    0x0B, 0x0B, 0x0B, 0x0B,                   // 4x MODE3
    0x0B, 0x0B, 0x0B, 0x0B,                   // 4x MODE3
    0x0B, 0x0B, 0x0B, 0x0B,                   // 4x MODE3
    0x0B, 0x0B, 0x0B, 0x0B,                   // 4x MODE3
    0x0B, 0x0B, 0x0B, 0x0B, 0x0B,             // 5x MODE3 => 25 MODE3 lines
    0x82, 0x00, 0x70                          // JMP to begin of DL and wait for Vertical BLank
};

static const uint16_t multi_mode_video_offset = 0x8000;
static const uint16_t multi_mode_color_offset = 0xA800;
static const uint16_t multi_mode_bkgnd_offset = 0xD000;
static const uint16_t multi_mode_dl_offset = 0xF800;
static const uint8_t __attribute__((aligned(4))) multi_mode_dl[] = {
    0x73, 0x00, 0x80, 0x00, 0xA8, 0x00, 0xD0,       // LMS + LFS + LBS
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, // 8x MODE5 => 30*8 = 240 lines of MODE5
    0x82, 0x00, 0xF8                                // JMP to begin of DL and wait for Vertical BLank
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
static const uint16_t affine_mode_dl_offset = 0xF800;
static const uint8_t __attribute__((aligned(4))) affine_mode_dl[] = {
    0x13, 0x00, 0x00, // LMS
    0x0F, 0x0F,       // 2x MODE7
    0x82, 0x00, 0xF8  // JMP to begin of DL and wait for Vertical BLank
};
static const uint16_t mixed_video_offset = 0xEA00;
static const uint16_t mixed_color_offset = 0xEC00;
static const uint16_t mixed_bkgnd_offset = 0xEE00;
static const uint16_t mixed_chrgn_offset = 0xE200;
static const uint8_t __attribute__((aligned(4))) mixed_mode_dl[] = {
    0x13, 0x00, 0x00,                                                // LMS
    0x14, 0x00,                                                      // border_columns = 0
    0x24, 0x9f,                                                      // row_height = 159
    0x34, (7 << 4) | 7,                                              // texture_bits = 7,7
    0x0F,                                                            // MODE7
    0x70,                                                            // 8x empty lines
    0xF3,                                                            // LMS + LFS + LBS + LCG
    (mixed_video_offset & 0xFF), ((mixed_video_offset >> 8) & 0xFF), // LMS
    (mixed_color_offset & 0xFF), ((mixed_color_offset >> 8) & 0xFF), // LFS
    (mixed_bkgnd_offset & 0xFF), ((mixed_bkgnd_offset >> 8) & 0xFF), // LBS
    (mixed_chrgn_offset & 0xFF), ((mixed_chrgn_offset >> 8) & 0xFF), // LCG
    0x14, 0x04,                                                      // border_columns = 4
    0x24, 0x07,                                                      // row_height = 7
    0x34, 0x00,                                                      // stride = 0
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,                  // 8x MODE2
    0x82, 0x00, 0xF8                                                 // JMP to begin of DL and wait for Vertical BLank
};
