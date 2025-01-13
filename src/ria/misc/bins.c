#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../cgia/example_data.h"
#include "../cgia/font_8.h"

#define NELEMS(x) (sizeof(x) / sizeof((x)[0]))

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
    fputc(0xff, file);
    fputc(0xff, file);
    fputc(text_mode_dl_offset & 0xFF, file);
    fputc(text_mode_dl_offset >> 8, file);
    fputc((text_mode_dl_offset + NELEMS(text_mode_dl) - 1) & 0xFF, file);
    fputc((text_mode_dl_offset + NELEMS(text_mode_dl) - 1) >> 8, file);
    for (size_t i = 0; i < NELEMS(text_mode_dl); ++i)
        fputc(text_mode_dl[i], file);
    fclose(file);

    file = fopen("affine_mode_dl.xex", "w");
    assert(file);
    fputc(0xff, file);
    fputc(0xff, file);
    fputc(affine_mode_dl_offset & 0xFF, file);
    fputc(affine_mode_dl_offset >> 8, file);
    fputc((affine_mode_dl_offset + NELEMS(affine_mode_dl) - 1) & 0xFF, file);
    fputc((affine_mode_dl_offset + NELEMS(affine_mode_dl) - 1) >> 8, file);
    for (size_t i = 0; i < NELEMS(affine_mode_dl); ++i)
        fputc(affine_mode_dl[i], file);
    fclose(file);

    file = fopen("mixed_mode_dl.xex", "w");
    assert(file);
    fputc(0xff, file);
    fputc(0xff, file);
    fputc(mixed_mode_dl_offset & 0xFF, file);
    fputc(mixed_mode_dl_offset >> 8, file);
    fputc((mixed_mode_dl_offset + NELEMS(mixed_mode_dl) - 1) & 0xFF, file);
    fputc((mixed_mode_dl_offset + NELEMS(mixed_mode_dl) - 1) >> 8, file);
    for (size_t i = 0; i < NELEMS(mixed_mode_dl); ++i)
        fputc(mixed_mode_dl[i], file);
    fclose(file);

    return 0;
}
