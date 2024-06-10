#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "font_8px.h"

#define SCREEN_COLUMNS 16

int main()
{
    uint8_t *data = header_data;
    uint8_t pixel[3];

    uint8_t bitmap = 0;
    uint8_t bm[SCREEN_COLUMNS][8];

    printf("static uint8_t __attribute__((aligned(4))) font8_data[2048] = {\n");

    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            uint8_t color_index = *data;

            HEADER_PIXEL(data, pixel)

            uint8_t row = y / 8;

            uint8_t column = x / 8;
            uint8_t bit = 7 - x % 8;

            if (color_index)
            {
                // set bitmap pixel to 1
                bitmap |= (1 << bit);
            }
            else
            {
                // set bitmap pixel to 0
                bitmap &= ~(1 << bit);
            }

            if (bit == 0)
            {
                uint8_t character_row = y % 8;
                bm[column][character_row] = bitmap;

                if (column == (SCREEN_COLUMNS - 1) && character_row == 7)
                {
                    for (int c = 0; c < SCREEN_COLUMNS; ++c)
                    {
                        for (int r = 0; r < 8; ++r)
                        {
                            printf("0x%02x, ", bm[c][r]);
                        }
                        printf(" // %02x\n", row * SCREEN_COLUMNS + c);
                    }
                }
            }
        }

    printf("};\n\n");

    return 0;
}
