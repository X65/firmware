/**
    4th bit unset (0-7) - instructions:
    Bits 0-2 encode the instruction:
    0 - empty lines filled with background color
    1 - duplicate lines - copy last raster line n-times
    2 - JMP Display List (Load DL offset address)
        - DLI bit set - wait for Vertical Blank
    3 - Load Memory - bits 4-6 flag which offsets will follow
        4 - LMS - memory scan
        5 - LFS - color scan
        6 - LBS - background scan
    4 - Load 8 bit value to Register Offset
    5 - Load 16 bit value to Register Offset
    ...

    4th bit set (8-15) - generate mode row:
    Bits 0-2 encode the mode:
    0 - HighRes text (80/96 columns) mode, 1 sprite only
    1 - HighRes bitmap mode, 2 colors FG+BG, 1 sprite only
    2 - text/tile mode
    3 - bitmap mode
    4 - multicolor text/tile mode
    5 - multicolor bitmap mode
    6 - doubled multicolor text/tile mode
    7 - doubled multicolor bitmap mode
*/

static uint8_t __attribute__((aligned(4))) hires_mode_dl[] = {
    0x70, 0x70, 0x30,                               // 2x 8 + 1x 4 of empty background lines
    0x73, 0xAB, 0xAD, 0xBA, 0xDA, 0xB0, 0x00,       // LMS + LFS + LBS
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, // 8x MODE2
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, // 8x MODE2
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, // 8x MODE2
    0x0A,                                           // 1x MODE2
    0x82, 0xCA, 0xFE                                // JMP to begin of DL and wait for Vertical BLank
};
