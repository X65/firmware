/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _VGA_TERM_COLOR_H_
#define _VGA_TERM_COLOR_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PICO_SCANVIDEO_ALPHA_MASK               0xff000000u
#define PICO_SCANVIDEO_PIXEL_RSHIFT             16u
#define PICO_SCANVIDEO_PIXEL_GSHIFT             8u
#define PICO_SCANVIDEO_PIXEL_BSHIFT             0u
#define PICO_SCANVIDEO_PIXEL_FROM_RGB8(r, g, b) (((b) << PICO_SCANVIDEO_PIXEL_BSHIFT) | ((g) << PICO_SCANVIDEO_PIXEL_GSHIFT) | ((r) << PICO_SCANVIDEO_PIXEL_RSHIFT))

extern const uint32_t color_256[256];

#endif /* _VGA_TERM_COLOR_H_ */
