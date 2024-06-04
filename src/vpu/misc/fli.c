#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// include a 121 colors indexed palette image exported form GIMP
#include "carrion-One_Zak_And_His_Kracken.h"

#define SCREEN_COLUMNS 40
#define SCREEN_ROWS    240

#include "../cgia/cgia_palette.h"

int8_t index2cgia[128] = {
    -1, -1, -1, -1, -1, -1, -1, -1, //
    -1, -1, -1, -1, -1, -1, -1, -1, //
    -1, -1, -1, -1, -1, -1, -1, -1, //
    -1, -1, -1, -1, -1, -1, -1, -1, //
    -1, -1, -1, -1, -1, -1, -1, -1, //
    -1, -1, -1, -1, -1, -1, -1, -1, //
    -1, -1, -1, -1, -1, -1, -1, -1, //
    -1, -1, -1, -1, -1, -1, -1, -1, //
    -1, -1, -1, -1, -1, -1, -1, -1, //
    -1, -1, -1, -1, -1, -1, -1, -1, //
    -1, -1, -1, -1, -1, -1, -1, -1, //
    -1, -1, -1, -1, -1, -1, -1, -1, //
    -1, -1, -1, -1, -1, -1, -1, -1, //
    -1, -1, -1, -1, -1, -1, -1, -1, //
    -1, -1, -1, -1, -1, -1, -1, -1, //
    -1, -1, -1, -1, -1, -1, -1, -1, //
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

    int fg[SCREEN_COLUMNS][SCREEN_ROWS];
    int bg[SCREEN_COLUMNS][SCREEN_ROWS];

    uint8_t bitmap = 0;
    uint8_t bm[SCREEN_COLUMNS][8];

    uint8_t bg1 = 0;
    int bg2 = -1;

    printf("static uint8_t __attribute__((aligned(4))) bitmap_data[9600] = {\n");

    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            uint8_t color_index = *data;
            assert(color_index < 128);

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

            if (x == 0)
            {
                // start of attribute row
                for (int i = 0; i < SCREEN_COLUMNS; ++i)
                {
                    fg[i][y] = -1;
                    bg[i][y] = -1;
                }
            }

            uint8_t column = x / 4;
            uint8_t bits = 3 - x % 4;

            int8_t fg_color = fg[column][y];
            int8_t bg_color = bg[column][y];

            if (current_color != bg1 && current_color != bg2)
            {
                if (fg_color < 0)
                {
                    fg_color = current_color;
                    fg[column][y] = fg_color;
                }
                else if (bg_color < 0 && fg_color != current_color)
                {
                    bg_color = current_color;
                    bg[column][y] = bg_color;
                }
            }

            if (current_color == bg1)
            {
                // black background
                // do nothing - keep bits %00
            }
            else if (current_color == bg2)
            {
                // set bitmap pixels to %11 - "second" background
                bitmap |= (0b11 << (bits * 2));
            }
            else if (fg_color == current_color)
            {
                // set bitmap pixels to %10
                bitmap |= (0b10 << (bits * 2));
            }
            else if (bg_color == current_color)
            {
                // set bitmap pixels to %01
                bitmap |= (0b01 << (bits * 2));
            }
            else
            {
                if (bg2 < 0)
                {
                    bg2 = current_color;
                }
                // set bitmap pixels to %11 - "second" background
                bitmap |= (0b11 << (bits * 2));
            }

            if (bits == 0)
            {
                bm[column][y] = bitmap;
                bitmap = 0;

                if (column == 39)
                {
                    for (int c = 0; c < SCREEN_COLUMNS; ++c)
                    {
                        printf("0x%02x, ", bm[c][y]);
                    }
                    printf(" // %d\n", y);
                }
            }
        }

    printf("};\n\n");

    printf("static uint8_t __attribute__((aligned(4))) colour_data[9600] = {\n");
    for (int r = 0; r < SCREEN_ROWS; ++r)
    {
        for (int c = 0; c < SCREEN_COLUMNS; ++c)
        {
            printf("0x%02x, ", fg[c][r] < 0 ? 0 : fg[c][r]);
        }
        printf(" // %d\n", r);
    }
    printf("};\n\n");

    printf("static uint8_t __attribute__((aligned(4))) background_data[9600] = {\n");
    for (int r = 0; r < SCREEN_ROWS; ++r)
    {
        for (int c = 0; c < SCREEN_COLUMNS; ++c)
        {
            printf("0x%02x, ", bg[c][r] < 0 ? 0 : bg[c][r]);
        }
        printf(" // %d\n", r);
    }
    printf("};\n");

    printf("static uint8_t background_color_1 = %d;\n", bg1);
    printf("static uint8_t background_color_2 = %d;\n", bg2);

    return 0;
}
