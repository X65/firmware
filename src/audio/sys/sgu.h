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

#include "sgu-1/sgu.h"
#include <stdbool.h>
#include <stdint.h>

// The register window is $FEC0..$FEFF, $FEFF selects what occupies the rest:
// $00..$08 a channel, $FF the service bank, anything else is reserved.
// The X65 emulator (doc/sgu-service-bank.md, src/chips/sgu1.c) is the spec.
#define SGU1_PCM_BANKS (4)

#define SGU1_SERVICE_BANK (0xFF)

#define SGU1_SVC_MAGIC         (0x00) // $00..$03, "SGU1"
#define SGU1_SVC_MAGIC_END     (0x03)
#define SGU1_SVC_VER_MAJOR     (0x04)
#define SGU1_SVC_VER_MINOR     (0x05)
#define SGU1_SVC_UNIQUE_ID     (0x06) // $06..$0D, 8 bytes
#define SGU1_SVC_UNIQUE_ID_END (0x0D)
#define SGU1_SVC_UNIQUE_ID_LEN (8)
#define SGU1_SVC_PCM_BANKS     (0x0E)
#define SGU1_SVC_SVC_BANKS     (0x0F)
#define SGU1_SVC_STATUS        (0x10)
#define SGU1_SVC_CHIP_RESET    (0x18)
#define SGU1_SVC_SAMPLE_OFF_LO (0x1C)
#define SGU1_SVC_SAMPLE_OFF_HI (0x1D)
#define SGU1_SVC_SAMPLE_BANK   (0x1E)
#define SGU1_SVC_SAMPLE_DATA   (0x1F)
#define SGU1_SVC_MASTER_VOL    (0x20)

#define SGU1_VERSION_MAJOR (0x01)
#define SGU1_VERSION_MINOR (0x00)

// STATUS ($10) bits. Read-to-clear.
#define SGU1_STATUS_CLIP (1 << 0) // the output stage saturated at least once

// CHIP_RESET ($18): the high nybble must be $A or the write is ignored.
// The low nybble names what to reset.
#define SGU1_RESET_MAGIC      (0xA0)
#define SGU1_RESET_MAGIC_MASK (0xF0)
#define SGU1_RESET_VOICES     (1 << 0) // -> SGU_RESET_VOICES
#define SGU1_RESET_TIMEBASE   (1 << 1) // -> SGU_RESET_TIMEBASE
#define SGU1_RESET_MIX        (1 << 2) // -> SGU_RESET_MIX
#define SGU1_RESET_SVC        (1 << 3) // the service registers

typedef struct
{
    struct SGU sgu;
    uint8_t selected_channel;
    // service bank, touched only from the host SPI ISR,
    // except master volume which the render core reads every sample
    uint16_t svc_sample_offset;
    uint8_t svc_sample_bank;
    volatile uint8_t svc_master_vol;
    uint32_t svc_status;      // STATUS latch, read-to-clear
    volatile uint32_t sample; // two signed PCM samples packed: [31:16] Left, [15:0] Right
} sgu1_t;

extern sgu1_t sgu_instance;

// initialize a new sgu1_t instance
void sgu_init(void);
// reset a sgu1_t instance, PCM memory is kept
void sgu_reset(void);

uint8_t sgu_reg_read(uint8_t reg);
void sgu_reg_write(uint8_t reg, uint8_t data);
