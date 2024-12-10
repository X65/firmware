#ifndef _CGIA_ENCODE_H
#define _CGIA_ENCODE_H

#include "pico.h"

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

uint32_t *__not_in_flash_func(cgia_encode_mode_7)(
    uint32_t *rgbbuf,
    uint32_t columns);

void __not_in_flash_func(cgia_encode_sprite)(
    uint32_t *rgbbuf,
    uint32_t *descriptor,
    uint8_t *line_data,
    uint32_t width);

#endif
