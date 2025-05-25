/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _OUT_H_
#define _OUT_H_

/** https://retrocomputing.stackexchange.com/a/13872
> In standard bitmap mode the C64 outputs 320 pixels in 40µs.
> The visible portion of a line is ~52µs; in 60Hz regions ~240 lines
> are considered 'visible', but in PAL regions it's ~288 lines.
> So if there were no borders, there'd be around 52/40*320 = 416 pixels
> across the visible portion of a line.
*/

// ANTIC generates 32/40/48 column text mode => max 256/320/384 px map mode
// ANTIC supports up to 240 Display List instructions
// With pixel-doubling this gives 768x480 mode, which has nice 16:10 aspect ratio
// running at 480p60 :-D
// Timings computed using https://tomverbeure.github.io/video_timings_calculator
// and porches adjusted to achieve 60Hz refresh rate with 366MHz SYS clock
#define MODE_H_ACTIVE_PIXELS 768
#define MODE_H_FRONT_PORCH   26
#define MODE_H_SYNC_WIDTH    82
#define MODE_H_BACK_PORCH    111
#define MODE_H_SYNC_POLARITY 0
#define MODE_V_ACTIVE_LINES  480
#define MODE_V_FRONT_PORCH   20
#define MODE_V_SYNC_WIDTH    42
#define MODE_V_BACK_PORCH    76
#define MODE_V_SYNC_POLARITY 1
#define MODE_V_FREQ_HZ       60

#define FB_H_REPEAT 2
#define FB_V_REPEAT 2

void out_init(void);
void out_post_reclock(void);
void out_print_status(void);

#endif /* _OUT_H_ */
