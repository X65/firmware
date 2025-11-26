#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../cgia/font_8.h"

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

    return 0;
}
