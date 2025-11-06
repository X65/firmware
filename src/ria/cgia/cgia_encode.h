#ifndef _CGIA_ENCODE_H
#define _CGIA_ENCODE_H

#include "pico.h"

#define CGIA_ENCODE_MODE_0(pixels, doubled, shared)                             \
    uint32_t *__not_in_flash_func(cgia_encode_mode_0##pixels##doubled##shared)( \
        uint32_t *rgbbuf,                                                       \
        uint32_t columns,                                                       \
        uint8_t *character_generator,                                           \
        uint32_t char_shift,                                                    \
        uint8_t shared_colors[8])

CGIA_ENCODE_MODE_0(_1bpp, , _shared);
CGIA_ENCODE_MODE_0(_1bpp, , _mapped);
CGIA_ENCODE_MODE_0(_1bpp, _doubled, _shared);
CGIA_ENCODE_MODE_0(_1bpp, _doubled, _mapped);
CGIA_ENCODE_MODE_0(_2bpp, , _shared);
CGIA_ENCODE_MODE_0(_2bpp, , _mapped);
CGIA_ENCODE_MODE_0(_2bpp, _doubled, _shared);
CGIA_ENCODE_MODE_0(_2bpp, _doubled, _mapped);
CGIA_ENCODE_MODE_0(_3bpp, , _shared);
CGIA_ENCODE_MODE_0(_3bpp, , _mapped);
CGIA_ENCODE_MODE_0(_3bpp, _doubled, _shared);
CGIA_ENCODE_MODE_0(_3bpp, _doubled, _mapped);
CGIA_ENCODE_MODE_0(_4bpp, , _shared);
CGIA_ENCODE_MODE_0(_4bpp, , _mapped);
CGIA_ENCODE_MODE_0(_4bpp, _doubled, _shared);
CGIA_ENCODE_MODE_0(_4bpp, _doubled, _mapped);

#define CGIA_ENCODE_MODE_1(pixels, doubled, shared)                             \
    uint32_t *__not_in_flash_func(cgia_encode_mode_1##pixels##doubled##shared)( \
        uint32_t *rgbbuf,                                                       \
        uint32_t columns,                                                       \
        uint8_t shared_colors[8])

CGIA_ENCODE_MODE_1(_1bpp, , _shared);
CGIA_ENCODE_MODE_1(_1bpp, , _mapped);
CGIA_ENCODE_MODE_1(_1bpp, _doubled, _shared);
CGIA_ENCODE_MODE_1(_1bpp, _doubled, _mapped);
CGIA_ENCODE_MODE_1(_2bpp, , _shared);
CGIA_ENCODE_MODE_1(_2bpp, , _mapped);
CGIA_ENCODE_MODE_1(_2bpp, _doubled, _shared);
CGIA_ENCODE_MODE_1(_2bpp, _doubled, _mapped);
CGIA_ENCODE_MODE_1(_3bpp, , _shared);
CGIA_ENCODE_MODE_1(_3bpp, , _mapped);
CGIA_ENCODE_MODE_1(_3bpp, _doubled, _shared);
CGIA_ENCODE_MODE_1(_3bpp, _doubled, _mapped);
CGIA_ENCODE_MODE_1(_4bpp, , _shared);
CGIA_ENCODE_MODE_1(_4bpp, , _mapped);
CGIA_ENCODE_MODE_1(_4bpp, _doubled, _shared);
CGIA_ENCODE_MODE_1(_4bpp, _doubled, _mapped);

uint32_t *__not_in_flash_func(cgia_encode_mode_2_shared)(
    uint32_t *rgbbuf,
    uint32_t columns,
    uint8_t *character_generator,
    uint32_t char_shift);
uint32_t *__not_in_flash_func(cgia_encode_mode_2_mapped)(
    uint32_t *rgbbuf,
    uint32_t columns,
    uint8_t *character_generator,
    uint32_t char_shift);
uint32_t *__not_in_flash_func(cgia_encode_vt)(
    uint32_t *rgbbuf,
    uint32_t columns,
    uint8_t *character_generator,
    uint32_t char_shift);

uint32_t *__not_in_flash_func(cgia_encode_mode_3_shared)(
    uint32_t *rgbbuf,
    uint32_t columns);
uint32_t *__not_in_flash_func(cgia_encode_mode_3_mapped)(
    uint32_t *rgbbuf,
    uint32_t columns);

uint32_t *__not_in_flash_func(cgia_encode_mode_4_shared)(
    uint32_t *rgbbuf,
    uint32_t columns,
    uint8_t *character_generator,
    uint32_t char_shift,
    uint8_t shared_colors[2]);

uint32_t *__not_in_flash_func(cgia_encode_mode_4_mapped)(
    uint32_t *rgbbuf,
    uint32_t columns,
    uint8_t *character_generator,
    uint32_t char_shift,
    uint8_t shared_colors[2]);

uint32_t *__not_in_flash_func(cgia_encode_mode_4_doubled_shared)(
    uint32_t *rgbbuf,
    uint32_t columns,
    uint8_t *character_generator,
    uint32_t char_shift,
    uint8_t shared_colors[2]);

uint32_t *__not_in_flash_func(cgia_encode_mode_4_doubled_mapped)(
    uint32_t *rgbbuf,
    uint32_t columns,
    uint8_t *character_generator,
    uint32_t char_shift,
    uint8_t shared_colors[2]);

uint32_t *__not_in_flash_func(cgia_encode_mode_5_shared)(
    uint32_t *rgbbuf,
    uint32_t columns,
    uint8_t shared_colors[2]);

uint32_t *__not_in_flash_func(cgia_encode_mode_5_mapped)(
    uint32_t *rgbbuf,
    uint32_t columns,
    uint8_t shared_colors[2]);

uint32_t *__not_in_flash_func(cgia_encode_mode_5_doubled_shared)(
    uint32_t *rgbbuf,
    uint32_t columns,
    uint8_t shared_colors[2]);

uint32_t *__not_in_flash_func(cgia_encode_mode_5_doubled_mapped)(
    uint32_t *rgbbuf,
    uint32_t columns,
    uint8_t shared_colors[2]);

uint32_t *__not_in_flash_func(cgia_encode_mode_6)(
    uint32_t *rgbbuf,
    uint32_t columns,
    uint8_t base_color[8],
    uint8_t back_color);

uint32_t *__not_in_flash_func(cgia_encode_mode_6_doubled)(
    uint32_t *rgbbuf,
    uint32_t columns,
    uint8_t base_color[8],
    uint8_t back_color);

uint32_t *__not_in_flash_func(cgia_encode_mode_7)(
    uint32_t *rgbbuf,
    uint32_t columns);

void __not_in_flash_func(cgia_encode_sprite)(
    uint32_t *rgbbuf,
    uint32_t *descriptor,
    uint8_t *line_data,
    uint32_t width);

#endif
