/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _KBD_ENG_H_
#define _KBD_ENG_H_

// English

// KEYCODE to Unicode Conversion
// {without shift, with shift, with altGr, with shift and altGr}

// Include both ISO_639-1 and ISO_639-2 codes
// when submitting new keyboard layouts.

#define HID_KEYCODE_TO_UNICODE_EN           HID_KEYCODE_TO_UNICODE_ENG_US_QWERTY
#define HID_KEYCODE_TO_UNICODE_EN_US        HID_KEYCODE_TO_UNICODE_ENG_US_QWERTY
#define HID_KEYCODE_TO_UNICODE_EN_US_QWERTY HID_KEYCODE_TO_UNICODE_ENG_US_QWERTY
#define HID_KEYCODE_TO_UNICODE_ENG          HID_KEYCODE_TO_UNICODE_ENG_US_QWERTY
#define HID_KEYCODE_TO_UNICODE_ENG_US       HID_KEYCODE_TO_UNICODE_ENG_US_QWERTY

#define HID_KEYCODE_TO_UNICODE_ENG_US_QWERTY \
    /*     0x00 */ {0, 0, 0, 0},             \
        /* 0x01 */ {0, 0, 0, 0},             \
        /* 0x02 */ {0, 0, 0, 0},             \
        /* 0x03 */ {0, 0, 0, 0},             \
        /* 0x04 */ {'a', 'A', 0, 0},         \
        /* 0x05 */ {'b', 'B', 0, 0},         \
        /* 0x06 */ {'c', 'C', 0, 0},         \
        /* 0x07 */ {'d', 'D', 0, 0},         \
        /* 0x08 */ {'e', 'E', 0, 0},         \
        /* 0x09 */ {'f', 'F', 0, 0},         \
        /* 0x0a */ {'g', 'G', 0, 0},         \
        /* 0x0b */ {'h', 'H', 0, 0},         \
        /* 0x0c */ {'i', 'I', 0, 0},         \
        /* 0x0d */ {'j', 'J', 0, 0},         \
        /* 0x0e */ {'k', 'K', 0, 0},         \
        /* 0x0f */ {'l', 'L', 0, 0},         \
        /* 0x10 */ {'m', 'M', 0, 0},         \
        /* 0x11 */ {'n', 'N', 0, 0},         \
        /* 0x12 */ {'o', 'O', 0, 0},         \
        /* 0x13 */ {'p', 'P', 0, 0},         \
        /* 0x14 */ {'q', 'Q', 0, 0},         \
        /* 0x15 */ {'r', 'R', 0, 0},         \
        /* 0x16 */ {'s', 'S', 0, 0},         \
        /* 0x17 */ {'t', 'T', 0, 0},         \
        /* 0x18 */ {'u', 'U', 0, 0},         \
        /* 0x19 */ {'v', 'V', 0, 0},         \
        /* 0x1a */ {'w', 'W', 0, 0},         \
        /* 0x1b */ {'x', 'X', 0, 0},         \
        /* 0x1c */ {'y', 'Y', 0, 0},         \
        /* 0x1d */ {'z', 'Z', 0, 0},         \
        /* 0x1e */ {'1', '!', 0, 0},         \
        /* 0x1f */ {'2', '@', 0, 0},         \
        /* 0x20 */ {'3', '#', 0, 0},         \
        /* 0x21 */ {'4', '$', 0, 0},         \
        /* 0x22 */ {'5', '%', 0, 0},         \
        /* 0x23 */ {'6', '^', 0, 0},         \
        /* 0x24 */ {'7', '&', 0, 0},         \
        /* 0x25 */ {'8', '*', 0, 0},         \
        /* 0x26 */ {'9', '(', 0, 0},         \
        /* 0x27 */ {'0', ')', 0, 0},         \
        /* 0x28 */ {'\r', '\r', 0, 0},       \
        /* 0x29 */ {'\x1b', '\x1b', 0, 0},   \
        /* 0x2a */ {'\x7f', '\x7f', 0, 0},   \
        /* 0x2b */ {'\t', '\t', 0, 0},       \
        /* 0x2c */ {' ', ' ', 0, 0},         \
        /* 0x2d */ {'-', '_', 0, 0},         \
        /* 0x2e */ {'=', '+', 0, 0},         \
        /* 0x2f */ {'[', '{', 0, 0},         \
        /* 0x30 */ {']', '}', 0, 0},         \
        /* 0x31 */ {'\\', '|', 0, 0},        \
        /* 0x32 */ {'\\', '|', 0, 0},        \
        /* 0x33 */ {';', ':', 0, 0},         \
        /* 0x34 */ {'\'', '\"', 0, 0},       \
        /* 0x35 */ {'`', '~', 0, 0},         \
        /* 0x36 */ {',', '<', 0, 0},         \
        /* 0x37 */ {'.', '>', 0, 0},         \
        /* 0x38 */ {'/', '?', 0, 0},         \
        /* 0x39 */ {0, 0, 0, 0},             \
        /* 0x3a */ {0, 0, 0, 0},             \
        /* 0x3b */ {0, 0, 0, 0},             \
        /* 0x3c */ {0, 0, 0, 0},             \
        /* 0x3d */ {0, 0, 0, 0},             \
        /* 0x3e */ {0, 0, 0, 0},             \
        /* 0x3f */ {0, 0, 0, 0},             \
        /* 0x40 */ {0, 0, 0, 0},             \
        /* 0x41 */ {0, 0, 0, 0},             \
        /* 0x42 */ {0, 0, 0, 0},             \
        /* 0x43 */ {0, 0, 0, 0},             \
        /* 0x44 */ {0, 0, 0, 0},             \
        /* 0x45 */ {0, 0, 0, 0},             \
        /* 0x46 */ {0, 0, 0, 0},             \
        /* 0x47 */ {0, 0, 0, 0},             \
        /* 0x48 */ {0, 0, 0, 0},             \
        /* 0x49 */ {0, 0, 0, 0},             \
        /* 0x4a */ {0, 0, 0, 0},             \
        /* 0x4b */ {0, 0, 0, 0},             \
        /* 0x4c */ {0, 0, 0, 0},             \
        /* 0x4d */ {0, 0, 0, 0},             \
        /* 0x4e */ {0, 0, 0, 0},             \
        /* 0x4f */ {0, 0, 0, 0},             \
        /* 0x50 */ {0, 0, 0, 0},             \
        /* 0x51 */ {0, 0, 0, 0},             \
        /* 0x52 */ {0, 0, 0, 0},             \
        /* 0x53 */ {0, 0, 0, 0},             \
        /* 0x54 */ {'/', '/', 0, 0},         \
        /* 0x55 */ {'*', '*', 0, 0},         \
        /* 0x56 */ {'-', '-', 0, 0},         \
        /* 0x57 */ {'+', '+', 0, 0},         \
        /* 0x58 */ {'\r', '\r', 0, 0},       \
        /* 0x59 */ {'1', 0, 0, 0},           \
        /* 0x5a */ {'2', 0, 0, 0},           \
        /* 0x5b */ {'3', 0, 0, 0},           \
        /* 0x5c */ {'4', 0, 0, 0},           \
        /* 0x5d */ {'5', 0, 0, 0},           \
        /* 0x5e */ {'6', 0, 0, 0},           \
        /* 0x5f */ {'7', 0, 0, 0},           \
        /* 0x60 */ {'8', 0, 0, 0},           \
        /* 0x61 */ {'9', 0, 0, 0},           \
        /* 0x62 */ {'0', 0, 0, 0},           \
        /* 0x63 */ {'.', 0, 0, 0},           \
        /* 0x64 */ {'\\', '|', 0, 0},        \
        /* 0x65 */ {0, 0, 0, 0},             \
        /* 0x66 */ {0, 0, 0, 0},             \
        /* 0x67 */ {'=', '=', 0, 0},

#endif /* _KBD_ENG_H_ */
