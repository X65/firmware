/*
 * Copyright (c) 2023 Rumbledethumps
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "term/term.h"
#include "pico/stdio/driver.h"
#include "pico/stdlib.h"
#include "sys/out.h"
#include "term/ansi.h"
#include "term/font.h"
#include <stdint.h>
#include <stdio.h>

// If you are extending this for use outside the Picocomputer,
// CSI codes with multiple parameters will need a more complete
// implementation first.

#define FONT_CHAR_WIDTH  8
#define FONT_CHAR_HEIGHT 8
#define FONT_N_CHARS     256
#define FONT_FIRST_ASCII 0

// Pixel format RGB222
uint8_t colors[] = {
    0b000000,
    0b100000,
    0b001000,
    0b101000,
    0b000010,
    0b100010,
    0b001010,
    0b101010,
    0b010101,
    0b110000,
    0b001100,
    0b111100,
    0b000011,
    0b110011,
    0b001111,
    0b111111,
};

#define CHAR_COLS (FRAME_WIDTH / FONT_CHAR_WIDTH)
#define CHAR_ROWS (FRAME_HEIGHT / FONT_CHAR_HEIGHT)

#define COLOUR_PLANE_ROW_WORDS  (CHAR_COLS * 4 / 32)
#define COLOUR_PLANE_SIZE_WORDS (CHAR_ROWS * COLOUR_PLANE_ROW_WORDS)
uint8_t charbuf[CHAR_ROWS * CHAR_COLS];
uint32_t colourbuf[3 * COLOUR_PLANE_SIZE_WORDS];

static inline void set_char(uint x, uint y, char c)
{
    if (x >= CHAR_COLS || y >= CHAR_ROWS)
        return;
    charbuf[x + y * CHAR_COLS] = c;
}

// Pixel format RGB222
static void set_colour222(uint x, uint y, uint8_t fg, uint8_t bg)
{
    if (x >= CHAR_COLS || y >= CHAR_ROWS)
        return;
    uint word_index = y * COLOUR_PLANE_ROW_WORDS + x / 8;
    uint bit_index = x % 8 * 4;
    for (int plane = 0; plane < 3; ++plane)
    {
        uint32_t fg_bg_combined = (fg & 0x3) | (bg << 2 & 0xc);
        colourbuf[word_index] = (colourbuf[word_index] & ~(0xfu << bit_index)) | (fg_bg_combined << bit_index);
        fg >>= 2;
        bg >>= 2;
        word_index += COLOUR_PLANE_SIZE_WORDS;
    }
}

static uint16_t get_colour222(uint x, uint y)
{
    if (x >= CHAR_COLS || y >= CHAR_ROWS)
        return 0;
    uint16_t fg = 0;
    uint16_t bg = 0;
    uint word_index = y * COLOUR_PLANE_ROW_WORDS + x / 8;
    uint bit_index = x % 8 * 4;
    for (int plane = 0; plane < 3; ++plane)
    {

        uint32_t fg_bg_combined = (colourbuf[word_index] & (0xfu << bit_index)) >> bit_index;
        fg |= (fg_bg_combined & 0x3) << (plane * 2);
        bg |= ((fg_bg_combined >> 2) & 0x3) << (plane * 2);
        word_index += COLOUR_PLANE_SIZE_WORDS;
    }
    return (bg << 8) | fg;
}

static inline void set_colour(uint x, uint y, uint8_t bg_fg)
{
    set_colour222(x, y, colors[bg_fg & 0x0f], colors[(bg_fg & 0xf0) >> 4]);
}

#define TERM_WORD_WRAP 1
static int term_x = 0, term_y = 0;
static int term_y_offset = 0;
static uint8_t term_color = 0x07;
static absolute_time_t term_timer = {0};
static int32_t term_blink_state = 0;
static ansi_state_t term_state = ansi_state_C0;
static int term_csi_param;

static void term_cursor_set_inv(bool inv)
{
    if (term_blink_state == -1 || inv == term_blink_state || term_x >= CHAR_COLS)
        return;
    const uint8_t line = (term_y + term_y_offset) % CHAR_ROWS;
    uint16_t fg_bg_combined = get_colour222(term_x, line);
    set_colour222(term_x, line, (fg_bg_combined & 0xff00) >> 8, (fg_bg_combined & 0x00ff));
    term_blink_state = inv;
}

static void term_out_sgr(int param)
{
    switch (param)
    {
    case -1:
    case 0: // reset
        term_color = 0x07;
        break;
    case 1: // bold intensity
        term_color = (term_color | 0x08);
        break;
    case 22: // normal intensity
        term_color = (term_color & 0xf7);
        break;
    case 30: // foreground color
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 37:
        term_color = (term_color & 0xf8) | (param - 30);
        break;
    case 40: // background color
    case 41:
    case 42:
    case 43:
    case 44:
    case 45:
    case 46:
    case 47:
        term_color = ((param - 40) << 4) | (term_color & 0x8f);
        break;
    }
}

static void term_out_ht()
{
    if (term_x < CHAR_COLS)
    {
        int xp = 8 - ((term_x + 8) & 7);
        term_x += xp;
    }
}

static void term_out_lf()
{
    if (++term_y == CHAR_ROWS)
    {
        term_y = CHAR_ROWS - 1;
        if (++term_y_offset == CHAR_ROWS)
        {
            term_y_offset = 0;
        }
        const uint8_t line = (term_y + term_y_offset) % CHAR_ROWS;
        uint8_t *line_ptr = charbuf + line * CHAR_COLS;
        for (size_t x = 0; x < CHAR_COLS; ++x)
        {
            line_ptr[x] = ' ';
            set_colour(x, line, term_color);
        }
    }
}

static void term_out_ff()
{
    term_x = term_y = 0;
    for (size_t i = 0; i < CHAR_ROWS * CHAR_COLS; ++i)
    {
        charbuf[i] = ' ';
        set_colour(i % CHAR_COLS, i / CHAR_COLS, term_color);
    }
}

static void term_out_cr()
{
    term_x = 0;
}

static void term_out_char(char ch)
{
    if (term_x == CHAR_COLS)
    {
        if (TERM_WORD_WRAP)
        {
            term_out_cr();
            term_out_lf();
        }
        else
        {
            term_x -= 1;
        }
    }
    const uint8_t line = (term_y + term_y_offset) % CHAR_ROWS;
    set_char(term_x, line, ch);
    set_colour(term_x, line, term_color);
    ++term_x;
}

// Cursor forward
static void term_out_cuf(int cols)
{
    if (cols > CHAR_COLS - term_x)
        cols = CHAR_COLS - term_x;
    term_x += cols;
}

// Cursor backward
static void term_out_cub(int cols)
{
    if (cols > term_x)
        cols = term_x;
    term_x -= cols;
}

// Delete characters
static void term_out_dch(int chars)
{
    const uint8_t line = (term_y + term_y_offset) % CHAR_ROWS;
    uint8_t *line_ptr = charbuf + line * CHAR_COLS;
    if (chars > CHAR_COLS - term_x)
        chars = CHAR_COLS - term_x;
    for (int x = term_x; x < CHAR_COLS; x++)
    {
        if (chars + x >= CHAR_COLS)
        {
            line_ptr[x] = ' ';
            set_colour(x, line, term_color);
        }
        else
        {
            line_ptr[x] = line_ptr[x + chars];
            uint16_t colour = get_colour222(x + chars, line);
            set_colour222(x, line, (colour & 0x00ff), (colour & 0xff00) >> 8);
        }
    }
}

static void term_out_state_C0(char ch)
{
    if (ch == '\b')
        term_out_cub(1);
    else if (ch == '\t')
        term_out_ht();
    else if (ch == '\n')
        term_out_lf();
    else if (ch == '\f')
        term_out_ff();
    else if (ch == '\r')
        term_out_cr();
    else if (ch == '\33')
        term_state = ansi_state_Fe;
    else if (ch >= 32 && ch <= 255)
        term_out_char(ch);
}

static void term_out_state_Fe(char ch)
{
    if (ch == '[')
    {
        term_state = ansi_state_CSI;
        term_csi_param = -1;
    }
    else
        term_state = ansi_state_C0;
}

static void term_out_state_CSI(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        if (term_csi_param < 0)
        {
            term_csi_param = ch - '0';
        }
        else
        {
            term_csi_param *= 10;
            term_csi_param += ch - '0';
        }
        return;
    }
    if (ch == ';')
    {
        // all codes with multiple parameters
        // end up here where we assume SGR
        term_out_sgr(term_csi_param);
        term_csi_param = -1;
        return;
    }
    term_state = ansi_state_C0;
    if (ch == 'm')
    {
        term_out_sgr(term_csi_param);
        return;
    }
    // Everything below defaults to 1
    if (term_csi_param < 0)
        term_csi_param = -term_csi_param;
    if (ch == 'C')
        term_out_cuf(term_csi_param);
    else if (ch == 'D')
        term_out_cub(term_csi_param);
    else if (ch == 'P')
        term_out_dch(term_csi_param);
}

static void term_out_chars(const char *buf, int length)
{
    if (length)
    {
        term_cursor_set_inv(false);
        for (int i = 0; i < length; i++)
        {
            char ch = buf[i];
            if (ch == ANSI_CANCEL)
                term_state = ansi_state_C0;
            else
                switch (term_state)
                {
                case ansi_state_C0:
                    term_out_state_C0(ch);
                    break;
                case ansi_state_Fe:
                    term_out_state_Fe(ch);
                    break;
                case ansi_state_CSI:
                    term_out_state_CSI(ch);
                    break;
                }
        }
        term_timer = get_absolute_time();
    }
}

static stdio_driver_t term_stdio = {
    .out_chars = term_out_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = PICO_STDIO_DEFAULT_CRLF
#endif
};

void term_init(void)
{
    // become part of stdout
    stdio_set_driver_enabled(&term_stdio, true);

    term_clear();
    // for (uint y = 0; y < CHAR_ROWS; ++y)
    // {
    //     for (uint x = 0; x < CHAR_COLS; ++x)
    //     {
    //         set_char(x, y, (x + y * CHAR_COLS) % FONT_N_CHARS + FONT_FIRST_ASCII);
    //         set_colour(x, y, (y << 4) | x);
    //     }
    // }
}

void term_task(void)
{
    absolute_time_t now = get_absolute_time();
    if (absolute_time_diff_us(now, term_timer) < 0)
    {
        term_cursor_set_inv(!term_blink_state);
        // 0.3ms drift to avoid blinking cursor trearing
        term_timer = delayed_by_us(now, 499700);
    }
}

void term_clear(void)
{
    // reset state and clear screen
    fputs("\30\33[0m\f", stdout);
    term_out_ff();
}

void term_render(uint y, uint32_t *tmdsbuf)
{
    const uint8_t font_line = y % FONT_CHAR_HEIGHT;
    uint8_t line = y / FONT_CHAR_HEIGHT + term_y_offset;
    if (line >= CHAR_ROWS)
        line -= CHAR_ROWS;

    for (int plane = 0; plane < 3; ++plane)
    {
        // tmds_encode_font_2bpp(
        //     (const uint8_t *)&charbuf[line * CHAR_COLS],
        //     &colourbuf[line * COLOUR_PLANE_ROW_WORDS + plane * COLOUR_PLANE_SIZE_WORDS],
        //     tmdsbuf + plane * (FRAME_WIDTH / DVI_SYMBOLS_PER_WORD),
        //     FRAME_WIDTH,
        //     (const uint8_t *)&font8[font_line * FONT_N_CHARS] - FONT_FIRST_ASCII);
    }
}
