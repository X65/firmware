#include "./sgu.h"
#include "hw.h"
#include <hardware/pio.h>
#include <pico/multicore.h>
#include <pico/rand.h>
#include <stdio.h>
#include <string.h>

sgu1_t sgu_instance;
#define SGU (&sgu_instance)

static void sgu_dump_channel_state(int channel)
{
    printf("-- %02X --\n", channel);
    uint8_t *ch = (uint8_t *)&SGU->su.chan[channel];
    printf("%02X %02X %02X %02X %02X %02X %02X %02X  ",
           ch[0], ch[1], ch[2], ch[3], ch[4], ch[5], ch[6], ch[7]);
    printf("%02X %02X %02X %02X %02X %02X %02X %02X\n",
           ch[8], ch[9], ch[10], ch[11], ch[12], ch[13], ch[14], ch[15]);
    printf("%02X %02X %02X %02X %02X %02X %02X %02X  ",
           ch[16], ch[17], ch[18], ch[19], ch[20], ch[21], ch[22], ch[23]);
    printf("%02X %02X %02X %02X %02X %02X %02X %02X\n",
           ch[24], ch[25], ch[26], ch[27], ch[28], ch[29], ch[30], ch[31]);
}

static inline void __attribute__((always_inline))
_sgu_tick(void)
{
    int16_t l, r;
    SoundUnit_NextSample(&SGU->su, &l, &r);
    ((int16_t *)(&SGU->sample))[1] = (int16_t)(((int)l + (int)SGU->rawL) >> 1);
    ((int16_t *)(&SGU->sample))[0] = (int16_t)(((int)r + (int)SGU->rawR) >> 1);
    SGU->rawL = l;
    SGU->rawR = r;
}

__attribute__((optimize("O3"))) static void __no_inline_not_in_flash_func(sgu_loop)(void)
{
    while (true)
    {
        while (!pio_interrupt_get(AUD_I2S_PIO, AUD_PIO_IRQ))
        {
            tight_loop_contents();
        }
        pio_interrupt_clear(AUD_I2S_PIO, AUD_PIO_IRQ);

        int runs = SGU1_OVERSAMPLING;
        do
        {
            _sgu_tick();
        } while (--runs);

        pio_sm_put_blocking(AUD_I2S_PIO, AUD_I2S_SM, SGU->sample);

        // check whether generation managed frame timing constraint
        if (pio_interrupt_get(AUD_I2S_PIO, AUD_PIO_IRQ))
        {
            printf("SGU tick overrun!\n");
            while (true)
            {
                // blink led or something
            }
        }
    }
}

void sgu_init()
{
    memset(SGU, 0, sizeof(*SGU));
    SoundUnit_Init(&SGU->su, SGU1_SAMPLE_MEM_SIZE, false);

    printf("Starting SGU core...\n");
    multicore_launch_core1(sgu_loop);
}

void sgu_reset()
{
    SoundUnit_Reset(&SGU->su);
    memset(SGU->reg, 0, sizeof(SGU->reg));
    SGU->sample = 0;
    SGU->rawL = SGU->rawR = 0;
}

static inline uint8_t _sgu_selected_channel()
{
    return SGU->reg[SGU1_REG_CHANNEL_SELECT] & (SGU1_NUM_CHANNELS - 1);
}

uint8_t sgu_reg_read(uint8_t reg)
{
    uint8_t data;
    if (reg < 32)
    {
        data = SGU->reg[reg];
    }
    else
    {
        uint8_t chan = _sgu_selected_channel();
        data = ((unsigned char *)SGU->su.chan)[chan << 5 | (reg & (SGU1_NUM_CHANNEL_REGS - 1))];
    }
    return data;
}

void sgu_reg_write(uint8_t reg, uint8_t data)
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
        uint8_t chan = _sgu_selected_channel();
        ((unsigned char *)SGU->su.chan)[chan << 5 | (reg & (SGU1_NUM_CHANNEL_REGS - 1))] = data;
    }
}
