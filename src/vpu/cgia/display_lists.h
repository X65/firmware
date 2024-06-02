/**
    4th bit unset (0-7) - instructions:
    Bits 0-2 encode the instruction:
    0 - empty lines filled with background color
    1 - JMP Display List (Load DL offset address)
        - LMS bit set - wait for Vertical Blank
        - DLI bit set - Load Color Map and Background Map
    2 ...
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
    0x70, 0x70, 0x30,                         // 2x 8 + 1x 4 of empty background lines
    0xCB, 0xAB, 0xAD, 0xBA, 0xDA, 0xB0, 0x00, // MODE3 + LMS + LFBS
    0x0B, 0x0B, 0x0B, 0x0B,                   // 4x MODE3
    0x0B, 0x0B, 0x0B, 0x0B,                   // 4x MODE3
    0x0B, 0x0B, 0x0B, 0x0B,                   // 4x MODE3
    0x0B, 0x0B, 0x0B, 0x0B,                   // 4x MODE3
    0x0B, 0x0B, 0x0B, 0x0B,                   // 4x MODE3
    0x0B, 0x0B, 0x0B, 0x0B,                   // 4x MODE3
    0xC1, 0xCA, 0xFE                          // JMP to begin of DL and wait for Vertical BLank
};
