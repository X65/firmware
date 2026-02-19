#pragma once
/*#
    # sgu1.h

    SGU-1 Sound Generator Unit 1

    ## Links

    - https://tildearrow.org/furnace/doc/latest/4-instrument/su.html

    ## 0BSD license

    Copyright (c) 2025 Tomasz Sterna

    Permission to use, copy, modify, and/or distribute this software for any
    purpose with or without fee is hereby granted.

    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

    ## Architecture — Dual-Core Audio Rendering

    Audio sample generation is split across both RP2350 cores using a
    map-reduce pattern over the 9 SGU channels.

         Core 1 (coordinator)                 Core 0 (worker)
         ~~~~~~~~~~~~~~~~~~~~                 ~~~~~~~~~~~~~~~~~
    PIO IRQ -> 1. Setup (LFO, envelope)      [main loop: USB,
                                                LED, SPI ISR]
               2. Push "go" to FIFO -------> FIFO IRQ fires
               3. Compute channels 0-4       3'. Compute channels 5-8
                     |                              |
                     v                              v
               4. Pop partial L,R <------------- Push partial L,R
               5. Merge + DC-removal HPF
               6. Push to PIO FIFO

    Channel split is 5+4 to balance core 1's setup/merge overhead against
    core 0's ISR entry cost. Wall time ~4000 cycles (vs ~7000 single-core).

    Ring modulation reads src[ch+1] directly -- cross-core boundary reads
    may see the previous or current frame's value, which is acceptable
    (1-sample jitter is inaudible).
#*/

#include "snd/sgu.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    struct SGU sgu;
    uint8_t selected_channel;
    volatile uint32_t sample; // two signed PCM samples packed: [31:16] Left, [15:0] Right
} sgu1_t;

extern sgu1_t sgu_instance;

// initialize a new sgu1_t instance
void sgu_init(void);
// reset a sgu1_t instance
void sgu_reset(void);

uint8_t sgu_reg_read(uint8_t reg);
void sgu_reg_write(uint8_t reg, uint8_t data);
