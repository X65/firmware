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
#include <speex_resampler.h>
#include <stdbool.h>
#include <stdint.h>

#define SGU1_AUDIO_CHANNELS (2)

#define SGU1_NUM_CHANNELS     (8)
#define SGU1_NUM_CHANNEL_REGS (32)

// control registers
#define SGU1_REG_CHANNEL_SELECT (0x00)

// channel registers
#define SGU1_CHAN_FREQ_LO         (0x00)
#define SGU1_CHAN_FREQ_HI         (0x01)
#define SGU1_CHAN_VOL             (0x02)
#define SGU1_CHAN_PAN             (0x03)
#define SGU1_CHAN_FLAGS0          (0x04)
#define SGU1_CHAN_FLAGS1          (0x05)
#define SGU1_CHAN_CUTOFF_LO       (0x06)
#define SGU1_CHAN_CUTOFF_HI       (0x07)
#define SGU1_CHAN_DUTY            (0x08)
#define SGU1_CHAN_RESON           (0x09)
#define SGU1_CHAN_PCMPOS_LO       (0x0A)
#define SGU1_CHAN_PCMPOS_HI       (0x0B)
#define SGU1_CHAN_PCMBND_LO       (0x0C)
#define SGU1_CHAN_PCMBND_HI       (0x0D)
#define SGU1_CHAN_PCMRST_LO       (0x0E)
#define SGU1_CHAN_PCMRST_HI       (0x0F)
#define SGU1_CHAN_SWFREQ_SPEED_LO (0x10)
#define SGU1_CHAN_SWFREQ_SPEED_HI (0x11)
#define SGU1_CHAN_SWFREQ_AMT      (0x12)
#define SGU1_CHAN_SWFREQ_BOUND    (0x13)
#define SGU1_CHAN_SWVOL_SPEED_LO  (0x14)
#define SGU1_CHAN_SWVOL_SPEED_HI  (0x15)
#define SGU1_CHAN_SWVOL_AMT       (0x16)
#define SGU1_CHAN_SWVOL_BOUND     (0x17)
#define SGU1_CHAN_SWCUT_SPEED_LO  (0x18)
#define SGU1_CHAN_SWCUT_SPEED_HI  (0x19)
#define SGU1_CHAN_SWCUT_AMT       (0x1A)
#define SGU1_CHAN_SWCUT_BOUND     (0x1B)
#define SGU1_CHAN_SPECIAL1C       (0x1C)
#define SGU1_CHAN_SPECIAL1D       (0x1D)
#define SGU1_CHAN_RESTIMER_LO     (0x1E)
#define SGU1_CHAN_RESTIMER_HI     (0x1F)

// channel control bits
#define SGU1_FLAGS0_WAVE_SHIFT    (0)
#define SGU1_FLAGS0_WAVE_MASK     (0x7 << SGU1_FLAGS0_WAVE_SHIFT)
#define SGU1_FLAGS0_PCM_SHIFT     (3)
#define SGU1_FLAGS0_PCM_MASK      (0x1 << SGU1_FLAGS0_PCM_SHIFT)
#define SGU1_FLAGS0_CONTROL_SHIFT (4)
#define SGU1_FLAGS0_CONTROL_MASK  (0xF << SGU1_FLAGS0_CONTROL_SHIFT)
#define SGU1_FLAGS0_CTL_RING_MOD  (1 << 4)
#define SGU1_FLAGS0_CTL_NSLOW     (1 << 5)
#define SGU1_FLAGS0_CTL_NSHIGH    (1 << 6)
#define SGU1_FLAGS0_CTL_NSBAND    (1 << 7)

#define SGU1_FLAGS0_WAVE_PULSE          (0)
#define SGU1_FLAGS0_WAVE_SAWTOOTH       (1)
#define SGU1_FLAGS0_WAVE_SINE           (2)
#define SGU1_FLAGS0_WAVE_TRIANGLE       (3)
#define SGU1_FLAGS0_WAVE_NOISE          (4)
#define SGU1_FLAGS0_WAVE_PERIODIC_NOISE (5)
#define SGU1_FLAGS0_WAVE_XOR_SINE       (6)
#define SGU1_FLAGS0_WAVE_XOR_TRIANGLE   (7)

#define SGU1_FLAGS1_PHASE_RESET        (1 << 0)
#define SGU1_FLAGS1_FILTER_PHASE_RESET (1 << 1)
#define SGU1_FLAGS1_PCM_LOOP           (1 << 2)
#define SGU1_FLAGS1_TIMER_SYNC         (1 << 3)
#define SGU1_FLAGS1_FREQ_SWEEP         (1 << 4)
#define SGU1_FLAGS1_VOL_SWEEP          (1 << 5)
#define SGU1_FLAGS1_CUT_SWEEP          (1 << 6)

#define CHIP_DIVIDER 2
#define CHIP_CLOCK   618000 // tSU: 6.18MHz (NTSC)

typedef struct
{
    SoundUnit su;
    uint8_t reg[32];
    SpeexResamplerState *resampler;
    float sample[SGU1_AUDIO_CHANNELS]; // Left, Right
} sgu1_t;

// initialize a new sgu1_t instance
void sgu1_init();
// reset a sgu1_t instance
void sgu1_reset();

uint8_t sgu1_reg_read(uint8_t reg);
void sgu1_reg_write(uint8_t reg, uint8_t data);
