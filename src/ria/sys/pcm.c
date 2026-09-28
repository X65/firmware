/*
 * Copyright (c) 2026 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/pcm.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "main.h"

#include <stdbool.h>
#include <string.h>

#if defined(DEBUG_RIA_SYS) || defined(DEBUG_RIA_SYS_PCM)
#include <stdio.h>
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
static inline void DBG(const char *fmt, ...)
{
    (void)fmt;
}
#endif

#define PCM_VOICES     4
// Bytes of sample memory. Offsets and 8-bit positions are 15-bit; bytes
// past the bank read $00 and drop writes. 32 KiB leaves the newlib heap
// only ~1 KiB of SRAM, so the bank stops at 24 KiB (BOOM!65 needs 20,176).
#define PCM_BANK_SIZE  0x6000
#define PCM_OFF_MASK   0x7FFF
#define PCM_BLOCK      32 // frames mixed per DMA half-buffer, 4 ms at 8 kHz
#define PCM_ID         0x50

#define PCM_FLAG_GATE     0x01
#define PCM_FLAG_LOOP     0x02
#define PCM_FLAG_UNSIGNED 0x04
#define PCM_FLAG_NIBBLE   0x08
#define PCM_FLAGS_MASK    0x0F

// Register offsets from PCM_REG_FIRST ($FEE3)
enum
{
    PCM_R_ID,
    PCM_R_BUSY,
    PCM_R_MASTER_VOL,
    PCM_R_OFF_LO,
    PCM_R_OFF_HI,
    PCM_R_DATA,
    PCM_R_VOICE_SEL,
    PCM_R_FLAGS,
    PCM_R_VOL,
    PCM_R_PAN,
    PCM_R_BANK_PAGES,
    PCM_R_RATE_LO,
    PCM_R_RATE_HI,
    PCM_R_POS_LO,
    PCM_R_POS_HI,
    PCM_R_END_LO,
    PCM_R_END_HI,
    PCM_R_LOOP_LO,
    PCM_R_LOOP_HI,
};

// The bus ISR writes the registers; the mixer, a lower priority IRQ,
// reads them and advances pos. Neither can interrupt the ISR, so byte
// read-modify-writes there are safe. pos_set tells the mixer the CPU
// wrote POS during a block, so the mixer must not write its own back.
typedef struct
{
    volatile uint8_t flags;
    volatile uint8_t vol;
    volatile int8_t pan;
    volatile bool pos_set;
    volatile uint16_t rate;
    volatile uint16_t pos;
    volatile uint16_t end;
    volatile uint16_t loop;
    uint32_t acc; // rate accumulator, mixer only
} pcm_voice_t;

static pcm_voice_t pcm_voices[PCM_VOICES];
static volatile uint8_t pcm_master_vol;
static volatile uint8_t pcm_voice_sel;
static volatile uint16_t pcm_upload_off;

static uint8_t __attribute__((aligned(4))) pcm_bank[PCM_BANK_SIZE];
_Static_assert(PCM_BANK_SIZE % 256 == 0 && PCM_BANK_SIZE <= 0x8000,
               "PCM_BANK_PAGES reports whole 256-byte pages of a 15-bit offset");

// DMA ping-pong: while one half plays, the mixer fills the other.
static uint32_t __attribute__((aligned(4))) pcm_frames[2][PCM_BLOCK];
static int32_t pcm_mix_l[PCM_BLOCK];
static int32_t pcm_mix_r[PCM_BLOCK];
static int pcm_dma_chan[2];

static inline uint16_t pcm_set_lo(uint16_t word, uint8_t data)
{
    return (uint16_t)((word & 0xFF00) | data);
}

static inline uint16_t pcm_set_hi(uint16_t word, uint8_t data)
{
    return (uint16_t)((word & 0x00FF) | (data << 8));
}

// A voice sounds while gated and either looping or short of its end.
static inline bool pcm_voice_busy(const pcm_voice_t *v)
{
    return (v->flags & PCM_FLAG_GATE)
           && ((v->flags & PCM_FLAG_LOOP) || v->pos < v->end);
}

// $00..$FF -> 0..256, so $FF is unity
static inline int32_t pcm_gain(uint8_t level)
{
    return level + (level >> 7);
}

// Centred sample at pos; past the bank is silence.
static inline int32_t pcm_fetch(uint16_t pos, uint8_t flags)
{
    if (flags & PCM_FLAG_NIBBLE)
    {
        const uint16_t off = pos >> 1;
        if (off >= PCM_BANK_SIZE)
            return 0;
        const uint8_t b = pcm_bank[off];
        const int32_t n = (pos & 1) ? (b >> 4) : (b & 0x0F);
        return n * 17 - 128;
    }
    const uint16_t off = pos & PCM_OFF_MASK;
    if (off >= PCM_BANK_SIZE)
        return 0;
    const uint8_t b = pcm_bank[off];
    if (flags & PCM_FLAG_UNSIGNED)
        return (int32_t)b - 128;
    return (int8_t)b;
}

// Upload port: bytes past the bank read $00 and drop writes, but the
// offset still advances, wrapping $7FFF -> $0000.
static inline uint8_t pcm_upload_read(void)
{
    const uint16_t off = pcm_upload_off;
    pcm_upload_off = (off + 1) & PCM_OFF_MASK;
    return off < PCM_BANK_SIZE ? pcm_bank[off] : 0x00;
}

static inline void pcm_upload_write(uint8_t data)
{
    const uint16_t off = pcm_upload_off;
    pcm_upload_off = (off + 1) & PCM_OFF_MASK;
    if (off < PCM_BANK_SIZE)
        pcm_bank[off] = data;
}

static void __not_in_flash_func(pcm_mix_voice)(pcm_voice_t *v)
{
    const uint8_t flags = v->flags;
    if (!(flags & PCM_FLAG_GATE))
        return;
    if (v->pos_set)
    {
        v->pos_set = false;
        v->acc = 0;
    }

    const bool looping = flags & PCM_FLAG_LOOP;
    const uint32_t rate = v->rate;
    const uint16_t end = v->end;
    const uint16_t loop = v->loop;
    uint16_t pos = v->pos;
    uint32_t acc = v->acc;

    // Balance law: centre plays both sides at full level
    const int32_t vol = pcm_gain(v->vol);
    const int32_t pan = v->pan;
    const int32_t gain_l = (pan > 0 ? 256 - 2 * pan : 256) * vol;
    const int32_t gain_r = (pan < 0 ? 256 + 2 * pan : 256) * vol;

    for (int f = 0; f < PCM_BLOCK; f++)
    {
        if (pos >= end && !looping)
            break; // parked
        const int32_t s = pcm_fetch(pos, flags);
        pcm_mix_l[f] += s * gain_l;
        pcm_mix_r[f] += s * gain_r;
        acc += rate;
        while (acc >= PCM_OUTPUT_RATE)
        {
            acc -= PCM_OUTPUT_RATE;
            if (pos < end)
            {
                if (++pos == end && looping)
                    pos = loop;
            }
            else if (looping)
                pos = loop;
        }
    }

    v->acc = acc;
    // Don't overwrite a POS the CPU wrote while this block was mixed.
    const uint32_t irq_status = save_and_disable_interrupts();
    if (!v->pos_set)
        v->pos = pos;
    restore_interrupts(irq_status);
}

static inline int16_t pcm_saturate(int32_t x)
{
    if (x > INT16_MAX)
        return INT16_MAX;
    if (x < INT16_MIN)
        return INT16_MIN;
    return (int16_t)x;
}

static void __not_in_flash_func(pcm_mix)(uint32_t *out)
{
    memset(pcm_mix_l, 0, sizeof(pcm_mix_l));
    memset(pcm_mix_r, 0, sizeof(pcm_mix_r));
    for (int i = 0; i < PCM_VOICES; i++)
        pcm_mix_voice(&pcm_voices[i]);

    // A full-scale voice at unity is +-2^23 in the accumulator.
    const int32_t master = pcm_gain(pcm_master_vol);
    for (int f = 0; f < PCM_BLOCK; f++)
    {
        const int16_t l = pcm_saturate(((pcm_mix_l[f] >> 8) * master) >> 8);
        const int16_t r = pcm_saturate(((pcm_mix_r[f] >> 8) * master) >> 8);
        // aud_i2s: left in the high half, right in the low half
        out[f] = (uint32_t)(uint16_t)l << 16 | (uint16_t)r;
    }
}

static void __not_in_flash_func(pcm_dma_irq_handler)(void)
{
    for (int i = 0; i < 2; i++)
    {
        const uint ch = (uint)pcm_dma_chan[i];
        if (!dma_irqn_get_channel_status(1, ch))
            continue;
        dma_irqn_acknowledge_channel(1, ch);
        // This half just finished and the other is playing: refill it.
        dma_channel_set_read_addr(ch, pcm_frames[i], false);
        dma_channel_set_trans_count(ch, PCM_BLOCK, false);
        pcm_mix(pcm_frames[i]);
    }
}

static void pcm_reset(void)
{
    // Gate first, so a mixer IRQ between these writes plays nothing.
    for (int i = 0; i < PCM_VOICES; i++)
        pcm_voices[i].flags = 0;
    for (int i = 0; i < PCM_VOICES; i++)
    {
        pcm_voice_t *v = &pcm_voices[i];
        v->vol = 0xFF;
        v->pan = 0;
        v->rate = PCM_OUTPUT_RATE;
        v->pos = 0;
        v->end = 0;
        v->loop = 0;
        v->pos_set = true;
    }
    pcm_master_vol = 0xFF;
    pcm_voice_sel = 0;
    pcm_upload_off = 0;
}

void pcm_init(void)
{
    pcm_reset();
    memset(pcm_frames, 0, sizeof(pcm_frames));

    pcm_dma_chan[0] = dma_claim_unused_channel(true);
    pcm_dma_chan[1] = dma_claim_unused_channel(true);
    for (int i = 0; i < 2; i++)
    {
        const uint ch = (uint)pcm_dma_chan[i];
        dma_channel_config c = dma_channel_get_default_config(ch);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, pio_get_dreq(AUD_I2S_PIO, AUD_I2S_SM, true));
        channel_config_set_chain_to(&c, (uint)pcm_dma_chan[i ^ 1]);
        dma_channel_configure(ch, &c,
                              &AUD_I2S_PIO->txf[AUD_I2S_SM],
                              pcm_frames[i], PCM_BLOCK, false);
        dma_irqn_acknowledge_channel(1, ch);
        dma_irqn_set_channel_enabled(1, ch, true);
    }

    // DMA_IRQ_0 is core 1's DVI. The bus ISR must preempt the mixer.
    irq_set_exclusive_handler(DMA_IRQ_1, pcm_dma_irq_handler);
    irq_set_priority(DMA_IRQ_1, PICO_LOWEST_IRQ_PRIORITY);
    irq_set_enabled(DMA_IRQ_1, true);

    dma_channel_start((uint)pcm_dma_chan[0]);
    DBG("PCM: DMA %d/%d, %u byte bank\n", pcm_dma_chan[0], pcm_dma_chan[1], PCM_BANK_SIZE);
}

void pcm_stop(void)
{
    pcm_reset();
}

uint8_t pcm_reg_read(uint8_t reg)
{
    const pcm_voice_t *v = &pcm_voices[pcm_voice_sel];
    switch (reg - PCM_REG_FIRST)
    {
    case PCM_R_ID:
        return PCM_ID;
    case PCM_R_BUSY:
    {
        uint8_t busy = 0;
        for (int i = 0; i < PCM_VOICES; i++)
            if (pcm_voice_busy(&pcm_voices[i]))
                busy |= 1u << i;
        return busy;
    }
    case PCM_R_MASTER_VOL:
        return pcm_master_vol;
    case PCM_R_OFF_LO:
        return (uint8_t)pcm_upload_off;
    case PCM_R_OFF_HI:
        return (uint8_t)(pcm_upload_off >> 8);
    case PCM_R_DATA:
        return pcm_upload_read();
    case PCM_R_BANK_PAGES:
        return PCM_BANK_SIZE >> 8;
    case PCM_R_VOICE_SEL:
        return pcm_voice_sel;
    case PCM_R_FLAGS:
        return v->flags;
    case PCM_R_VOL:
        return v->vol;
    case PCM_R_PAN:
        return (uint8_t)v->pan;
    case PCM_R_RATE_LO:
        return (uint8_t)v->rate;
    case PCM_R_RATE_HI:
        return (uint8_t)(v->rate >> 8);
    case PCM_R_POS_LO:
        return (uint8_t)v->pos;
    case PCM_R_POS_HI:
        return (uint8_t)(v->pos >> 8);
    case PCM_R_END_LO:
        return (uint8_t)v->end;
    case PCM_R_END_HI:
        return (uint8_t)(v->end >> 8);
    case PCM_R_LOOP_LO:
        return (uint8_t)v->loop;
    case PCM_R_LOOP_HI:
        return (uint8_t)(v->loop >> 8);
    }
    return 0x00;
}

void pcm_reg_write(uint8_t reg, uint8_t data)
{
    pcm_voice_t *v = &pcm_voices[pcm_voice_sel];
    switch (reg - PCM_REG_FIRST)
    {
    case PCM_R_MASTER_VOL:
        pcm_master_vol = data;
        break;
    case PCM_R_OFF_LO:
        pcm_upload_off = pcm_set_lo(pcm_upload_off, data);
        break;
    case PCM_R_OFF_HI:
        pcm_upload_off = pcm_set_hi(pcm_upload_off, data) & PCM_OFF_MASK;
        break;
    case PCM_R_DATA:
        pcm_upload_write(data);
        break;
    case PCM_R_VOICE_SEL:
        pcm_voice_sel = data & (PCM_VOICES - 1);
        break;
    case PCM_R_FLAGS:
        v->flags = data & PCM_FLAGS_MASK;
        break;
    case PCM_R_VOL:
        v->vol = data;
        break;
    case PCM_R_PAN:
        v->pan = (int8_t)data;
        break;
    case PCM_R_RATE_LO:
        v->rate = pcm_set_lo(v->rate, data);
        break;
    case PCM_R_RATE_HI:
        v->rate = pcm_set_hi(v->rate, data);
        break;
    case PCM_R_POS_LO:
        v->pos = pcm_set_lo(v->pos, data);
        v->pos_set = true;
        break;
    case PCM_R_POS_HI:
        v->pos = pcm_set_hi(v->pos, data);
        v->pos_set = true;
        break;
    case PCM_R_END_LO:
        v->end = pcm_set_lo(v->end, data);
        break;
    case PCM_R_END_HI:
        v->end = pcm_set_hi(v->end, data);
        break;
    case PCM_R_LOOP_LO:
        v->loop = pcm_set_lo(v->loop, data);
        break;
    case PCM_R_LOOP_HI:
        v->loop = pcm_set_hi(v->loop, data);
        break;
    }
}
