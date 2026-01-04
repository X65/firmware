#include "./sgu.h"
#include "hw.h"
#include "sys/aud.h"
#include <pico/multicore.h>
#include <string.h>

sgu1_t sgu1_instance;
#define SGU (&sgu1_instance)

static inline void __attribute__((always_inline))
_sgu1_tick(void)
{
    int16_t l, r;
    SoundUnit_NextSample(&SGU->su, &l, &r);
    SGU->sample[0] = (float)(((int)l + (int)SGU->prev_sample[0]) >> 1) / 32768.0f;
    SGU->sample[1] = (float)(((int)r + (int)SGU->prev_sample[1]) >> 1) / 32768.0f;
    SGU->prev_sample[0] = l;
    SGU->prev_sample[1] = r;
}

__attribute__((optimize("O3"))) static void __no_inline_not_in_flash_func(sgu_loop)(void)
{
    uint64_t next = time_us_64();
    uint64_t err = 0;

    uint32_t base_us = 1000000u / SGU1_CHIP_CLOCK;
    uint32_t rem_us = 1000000u % SGU1_CHIP_CLOCK;

    while (true)
    {
        next += base_us;
        err += rem_us;
        if (err >= SGU1_CHIP_CLOCK)
        {
            next += 1; // add one extra us
            err -= SGU1_CHIP_CLOCK;
        }

        while ((int64_t)(time_us_64() - next) < 0)
        {
            tight_loop_contents();
        }

        _sgu1_tick();
    }
}

void sgu1_init()
{
    memset(SGU, 0, sizeof(*SGU));
    SoundUnit_Init(&SGU->su, SGU1_SAMPLE_MEM_SIZE, false);

    multicore_launch_core1(sgu_loop);
}

void sgu1_reset()
{
    SoundUnit_Reset(&SGU->su);
    memset(SGU->reg, 0, sizeof(SGU->reg));
    SGU->sample[0] = SGU->sample[1] = 0.0f;
}

static inline uint8_t _sgu1_selected_channel()
{
    return SGU->reg[SGU1_REG_CHANNEL_SELECT] & (SGU1_NUM_CHANNELS - 1);
}

uint8_t sgu1_reg_read(uint8_t reg)
{
    uint8_t data;
    if (reg < 32)
    {
        data = SGU->reg[reg];
    }
    else
    {
        uint8_t chan = _sgu1_selected_channel();
        data = ((unsigned char *)SGU->su.chan)[chan << 5 | (reg & (SGU1_NUM_CHANNEL_REGS - 1))];
    }
    return data;
}

void sgu1_reg_write(uint8_t reg, uint8_t data)
{
    if (reg < 32)
    {
        if (reg == SGU1_REG_CHANNEL_SELECT)
        {
            SGU->reg[reg] = data;
        }
    }
    else
    {
        uint8_t chan = _sgu1_selected_channel();
        ((unsigned char *)SGU->su.chan)[chan << 5 | (reg & (SGU1_NUM_CHANNEL_REGS - 1))] = data;
    }
}
