#ifndef _TMDS_ENCODE_ZXSPECTRUM_H
#define _TMDS_ENCODE_ZXSPECTRUM_H

#include "pico/platform.h"

uint32_t *__not_in_flash_func(tmds_encode_border)(
    uint32_t *tmdsbuf,
    uint32_t colour,
    uint32_t columns);

uint32_t *__not_in_flash_func(tmds_encode_mode_3_shared)(
    uint32_t *tmdsbuf,
    uint32_t pixels,
    volatile uint8_t *background_colour);

uint32_t *__not_in_flash_func(tmds_encode_mode_3_mapped)(
    uint32_t *tmdsbuf,
    uint32_t pixels);

#endif
