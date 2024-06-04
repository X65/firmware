#ifndef _TMDS_ENCODE_ZXSPECTRUM_H
#define _TMDS_ENCODE_ZXSPECTRUM_H

#include "pico/platform.h"

uint32_t *__not_in_flash_func(tmds_encode_border)(
    uint32_t *tmdsbuf,
    uint32_t colour,
    uint32_t columns);

void __not_in_flash_func(load_scanline_buffer_shared)(
    uint32_t *scanline_buffer,
    uint32_t columns);
void __not_in_flash_func(load_scanline_buffer_mapped)(
    uint32_t *scanline_buffer,
    uint32_t columns);

uint32_t *__not_in_flash_func(tmds_encode_mode_3_shared)(
    uint32_t *tmdsbuf,
    uint32_t *scanline_buffer,
    uint32_t columns);
uint32_t *__not_in_flash_func(tmds_encode_mode_3_mapped)(
    uint32_t *tmdsbuf,
    uint32_t *scanline_buffer,
    uint32_t columns);

uint32_t *__not_in_flash_func(tmds_encode_mode_5_shared)(
    uint32_t *tmdsbuf,
    uint32_t *scanline_buffer,
    uint32_t columns);
uint32_t *__not_in_flash_func(tmds_encode_mode_5_mapped)(
    uint32_t *tmdsbuf,
    uint32_t *scanline_buffer,
    uint32_t columns);

#endif
