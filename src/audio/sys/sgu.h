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
#*/

#include "snd/su.h"
#include "sys/aud.h"
#include <stdbool.h>
#include <stdint.h>

#define SGU1_AUDIO_CHANNELS (2)

#define SGU1_NUM_CHANNELS     (8)
#define SGU1_NUM_CHANNEL_REGS (32)

// control registers
#define SGU1_REG_CHANNEL_SELECT (0x00)

typedef struct
{
    SoundUnit su;
    uint8_t reg[32];
    int16_t rawL, rawR;
    uint32_t sample; // two signed PCM samples packed: [31:16] Left, [15:0] Right
} sgu1_t;

extern sgu1_t sgu_instance;

// initialize a new sgu1_t instance
void sgu_init();
// reset a sgu1_t instance
void sgu_reset();

uint8_t sgu_reg_read(uint8_t reg);
void sgu_reg_write(uint8_t reg, uint8_t data);
