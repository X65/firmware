#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// include a 16 colors indexed palette image exported form GIMP
// this assumes that there are max 16 (0-15) entries in paletted image
// and builds a mapping to X65 pallette on flight
#include "veto-the_mill.h"

#define SCREEN_COLUMNS 40
#define SCREEN_ROWS    25

#include "../cgia/cgia_palette.h"

int8_t index2cgia[16] = {
    -1, -1, -1, -1, //
    -1, -1, -1, -1, //
    -1, -1, -1, -1, //
    -1, -1, -1, -1, //
};

int MOD(int x)
{
    if (x > 0)
        return x;
    return -x;
}

int main()
{
    uint8_t *data = header_data;
    uint8_t pixel[3];

    int8_t fg[SCREEN_COLUMNS][SCREEN_ROWS];
    int8_t bg[SCREEN_COLUMNS][SCREEN_ROWS];

    uint8_t bitmap = 0;
    uint8_t bm[SCREEN_COLUMNS][8];

    printf("static uint8_t __attribute__((aligned(4))) bitmap_data[8000] = {\n");

    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            uint8_t color_index = *data;
            assert(color_index < 16);

            HEADER_PIXEL(data, pixel)

            int8_t current_color = index2cgia[color_index];
            if (current_color < 0)
            {
                // find closest matching color
                int distance = INT_MAX;
                for (int c = 0; c < CGIA_COLORS_NUM; ++c)
                {
                    uint32_t color = cgia_rgb_palette[c];
                    uint32_t blue = (color & 0x0000ff) >> 0;
                    uint32_t green = (color & 0x00ff00) >> 8;
                    uint32_t red = (color & 0xff0000) >> 16;

                    int color_distance = MOD(pixel[0] - red) + MOD(pixel[1] - green) + MOD(pixel[2] - blue);
                    if (color_distance < distance)
                    {
                        distance = color_distance;
                        current_color = c;
                    }
                }
                index2cgia[color_index] = current_color;
            }

            uint8_t row = y / 8;

            if (x == 0 && y % 8 == 0)
            {
                // start of attribute row
                for (int i = 0; i < SCREEN_COLUMNS; ++i)
                {
                    fg[i][row] = -1;
                    bg[i][row] = -1;
                }
            }

            uint8_t column = x / 8;
            uint8_t bit = 7 - x % 8;

            int8_t fg_color = fg[column][row];
            int8_t bg_color = bg[column][row];

            if (fg_color < 0)
            {
                fg_color = current_color;
                fg[column][row] = fg_color;
            }
            else if (bg_color < 0 && fg_color != current_color)
            {
                bg_color = current_color;
                bg[column][row] = bg_color;
            }

            if (fg_color == current_color)
            {
                // set bitmap pixel to 1
                bitmap |= (1 << bit);
            }
            else if (bg_color == current_color)
            {
                // set bitmap pixel to 0
                bitmap &= ~(1 << bit);
            }
            else
            {
                fprintf(stderr, "Error: more than 2 colors per color cell. %dx%d\n", x, y);
                abort();
            }

            if (bit == 0)
            {
                uint8_t character_row = y % 8;
                bm[column][character_row] = bitmap;

                if (column == 39 && character_row == 7)
                {
                    for (int c = 0; c < SCREEN_COLUMNS; ++c)
                    {
                        for (int r = 0; r < 8; ++r)
                        {
                            printf("0x%02x, ", bm[c][r]);
                        }
                        printf(" // %d x %d\n", row, c);
                    }
                }
            }
        }

    printf("};\n\n");

    printf("static uint8_t __attribute__((aligned(4))) colour_data[1000] = {\n");
    for (int r = 0; r < SCREEN_ROWS; ++r)
    {
        for (int c = 0; c < SCREEN_COLUMNS; ++c)
        {
            printf("0x%02x, ", fg[c][r]);
        }
        printf(" // %d \n", r);
    }
    printf("};\n\n");

    printf("static uint8_t __attribute__((aligned(4))) background_data[1000] = {\n");
    for (int r = 0; r < SCREEN_ROWS; ++r)
    {
        for (int c = 0; c < SCREEN_COLUMNS; ++c)
        {
            printf("0x%02x, ", bg[c][r] < 0 ? 0 : bg[c][r]);
        }
        printf(" // %d \n", r);
    }
    printf("};\n");

    return 0;
}
