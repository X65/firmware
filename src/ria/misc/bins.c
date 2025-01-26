#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../cgia/font_8.h"
#include "./example_data.h"

#define NELEMS(x) (sizeof(x) / sizeof((x)[0]))

void fputw(uint16_t word, FILE *file)
{
    fputc(word & 0xFF, file);
    fputc(word >> 8, file);
}

void fput_header(uint16_t offset, uint16_t size, FILE *file)
{
    fputw(offset, file);            // from
    fputw(offset + size - 1, file); // to
}

int main(void)
{
    FILE *file;

    file = fopen("font_8px.bin", "w");
    assert(file);
    for (size_t i = 0; i < NELEMS(font8_data); ++i)
        fputc(font8_data[i], file);
    fclose(file);

    file = fopen("text_mode_dl.xex", "w");
    assert(file);
    fputw(0xffff, file);                                          // file header
    fput_header(text_mode_dl_offset, NELEMS(text_mode_dl), file); // block header
    for (size_t i = 0; i < NELEMS(text_mode_dl); ++i)
        fputc(text_mode_dl[i], file);
    fclose(file);

    file = fopen("affine_mode_dl.xex", "w");
    assert(file);
    fputw(0xffff, file);
    fput_header(affine_mode_dl_offset, NELEMS(affine_mode_dl), file);
    for (size_t i = 0; i < NELEMS(affine_mode_dl); ++i)
        fputc(affine_mode_dl[i], file);
    fclose(file);

    file = fopen("mixed_mode_dl.xex", "w");
    assert(file);
    fputw(0xffff, file);
    fput_header(mixed_mode_dl_offset, NELEMS(mixed_mode_dl), file);
    for (size_t i = 0; i < NELEMS(mixed_mode_dl); ++i)
        fputc(mixed_mode_dl[i], file);
    fclose(file);

    file = fopen("text_mode_cl.xex", "w");
    assert(file);
    fputw(0xffff, file);
    fput_header(0x0000, 40 * 25, file); // MS
    for (size_t i = 0; i < 40 * 25; ++i)
        if (i >= 10 * 40 && i < 15 * 40)
            switch (i / 40)
            {
            case 10:
            case 14:
                fputc(255, file);
                break;
            case 12:
                fputc(text_mode_hello[i % 40], file);
                break;
            default:
                fputc(0, file);
            }
        else
            fputc(i & 0xFF, file);
    fput_header(0x1000, 40 * 25, file); // CS
    for (size_t i = 0; i < 40 * 25; ++i)
        if (i >= 10 * 40 && i < 15 * 40)
            switch (i / 40)
            {
            case 10:
            case 14:
                fputc(0x98 + (i % 40), file);
                break;
            default:
                fputc(text_mode_fg_color, file);
            }
        else
            fputc(i & 0xFF, file);
    fput_header(0x2000, 40 * 25, file); // BS
    for (size_t i = 0; i < 40 * 25; ++i)
        if (i >= 10 * 40 && i < 15 * 40)
            fputc(text_mode_bg_color, file);
        else
            fputc(0xFF - (i & 0xFF), file);
    fclose(file);

    return 0;
}
