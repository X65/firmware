#include "./sgu.h"
#include "hw.h"
#include "sys/aud.h"

#include <string.h>

static sgu1_t sgu1_instance;
sgu1_t *sgu = &sgu1_instance;

void sgu1_init()
{
    memset(sgu, 0, sizeof(*sgu));
    SoundUnit_Init(&sgu->su, SGU1_SAMPLE_MEM_SIZE, false);
    sgu->resampler = speex_resampler_init(
        SGU1_AUDIO_CHANNELS,
        CHIP_CLOCK / CHIP_DIVIDER,
        AUD_OUT_HZ,
        SGU1_RESAMPLER_QUALITY, nullptr);
}

void sgu1_reset()
{
    SoundUnit_Reset(&sgu->su);
    memset(sgu->reg, 0, sizeof(sgu->reg));
    sgu->sample[0] = sgu->sample[1] = 0.0f;
    speex_resampler_reset_mem(sgu->resampler);
}

/* tick the sound generation, return true when new sample ready */
static void _sgu1_tick()
{
    short l, r;
    SoundUnit_NextSample(&sgu->su, &l, &r);
    float in[2] = {((float)l / 32767.0f), ((float)r / 32767.0f)};
    spx_uint32_t in_len = 1;  // 1 stereo frame
    spx_uint32_t out_len = 1; // room for 1 stereo frame
    speex_resampler_process_interleaved_float(sgu->resampler, in, &in_len, sgu->sample, &out_len);
}

static inline uint8_t _sgu1_selected_channel()
{
    return sgu->reg[SGU1_REG_CHANNEL_SELECT] & (SGU1_NUM_CHANNELS - 1);
}

uint8_t sgu1_reg_read(uint8_t reg)
{
    uint8_t data;
    if (reg < 32)
    {
        data = sgu->reg[reg];
    }
    else
    {
        uint8_t chan = _sgu1_selected_channel();
        data = ((unsigned char *)sgu->su.chan)[chan << 5 | (reg & (SGU1_NUM_CHANNEL_REGS - 1))];
    }
    return data;
}

void sgu1_reg_write(uint8_t reg, uint8_t data)
{
    if (reg < 32)
    {
        if (reg == SGU1_REG_CHANNEL_SELECT)
        {
            sgu->reg[reg] = data;
        }
    }
    else
    {
        uint8_t chan = _sgu1_selected_channel();
        ((unsigned char *)sgu->su.chan)[chan << 5 | (reg & (SGU1_NUM_CHANNEL_REGS - 1))] = data;
    }
}
