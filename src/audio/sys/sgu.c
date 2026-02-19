#include "./sgu.h"
#include "hw.h"
#include "sys/led.h"
#include <hardware/pio.h>
#include <math.h>
#include <pico/multicore.h>
#include <pico/rand.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

sgu1_t sgu_instance;
#define SGU (&sgu_instance)

static void sgu_dump_channel_state(int channel)
{
    printf("-- %02X --\n", channel);
    uint8_t *ch = (uint8_t *)&SGU->sgu.chan[channel];
    printf("%02X %02X %02X %02X %02X %02X %02X %02X  ",
           ch[0], ch[1], ch[2], ch[3], ch[4], ch[5], ch[6], ch[7]);
    printf("%02X %02X %02X %02X %02X %02X %02X %02X\n",
           ch[8], ch[9], ch[10], ch[11], ch[12], ch[13], ch[14], ch[15]);
    printf("%02X %02X %02X %02X %02X %02X %02X %02X  ",
           ch[16], ch[17], ch[18], ch[19], ch[20], ch[21], ch[22], ch[23]);
    printf("%02X %02X %02X %02X %02X %02X %02X %02X\n",
           ch[24], ch[25], ch[26], ch[27], ch[28], ch[29], ch[30], ch[31]);
    printf("%02X %02X %02X %02X %02X %02X %02X %02X  ",
           ch[32], ch[33], ch[34], ch[35], ch[36], ch[37], ch[38], ch[39]);
    printf("%02X %02X %02X %02X %02X %02X %02X %02X\n",
           ch[40], ch[41], ch[42], ch[43], ch[44], ch[45], ch[46], ch[47]);
    printf("%02X %02X %02X %02X %02X %02X %02X %02X  ",
           ch[48], ch[49], ch[50], ch[51], ch[52], ch[53], ch[54], ch[55]);
    printf("%02X %02X %02X %02X %02X %02X %02X %02X\n",
           ch[56], ch[57], ch[58], ch[59], ch[60], ch[61], ch[62], ch[63]);
}

static inline int16_t __force_inline __attribute__((optimize("O3")))
clamp(int32_t sample)
{
    if (sample < INT16_MIN)
        return INT16_MIN;
    else if (sample > INT16_MAX)
        return INT16_MAX;
    else
        return (int16_t)sample;
}

static inline void __force_inline __attribute__((optimize("O3")))
_sgu_tick(void)
{
    int32_t l, r;
    SGU_NextSample(&SGU->sgu, &l, &r);

    const int16_t left = clamp(l >> 1);
    const int16_t right = clamp(r >> 1);
    SGU->sample = ((uint32_t)(uint16_t)left << 16) | (uint16_t)right;
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

        _sgu_tick();

        pio_sm_put_blocking(AUD_I2S_PIO, AUD_I2S_SM, SGU->sample);
        led_blink_color(SGU->sample >> 4);

        // check whether generation managed frame timing constraint
        if (pio_interrupt_get(AUD_I2S_PIO, AUD_PIO_IRQ))
        {
            printf("SGU tick overrun!\n");
            led_blink_color(0x00FF00);
            while (true)
            {
            }
        }
    }
}

void sgu_init()
{
    memset(SGU, 0, sizeof(*SGU));
    SGU_Init(&SGU->sgu, SGU_PCM_RAM_SIZE);

    printf("Starting SGU core...\n");
    multicore_launch_core1(sgu_loop);
    // aud_print_status();
}

void sgu_reset()
{
    SGU_Reset(&SGU->sgu);
    SGU->sample = 0;
    SGU->selected_channel = 0;
}

uint8_t sgu_reg_read(uint8_t reg)
{
    uint8_t data;
    if (reg == SGU_REGS_PER_CH - 1)
    {
        data = SGU->selected_channel;
    }
    else
    {
        data = ((unsigned char *)SGU->sgu.chan)[((SGU->selected_channel % SGU_CHNS) << 6) | (reg & (SGU_REGS_PER_CH - 1))];
    }
    return data;
}

void sgu_reg_write(uint8_t reg, uint8_t data)
{
    if (reg == SGU_REGS_PER_CH - 1)
    {
        SGU->selected_channel = data;
    }
    else
    {
        // ((unsigned char*)SGU->sgu.chan)[((SGU->selected_channel % SGU_CHNS) << 6) | (reg & (SGU_REGS_PER_CH - 1))] =
        // data;
        SGU_Write(&SGU->sgu, (uint16_t)((SGU->selected_channel % SGU_CHNS) << 6) | (reg & (SGU_REGS_PER_CH - 1)), data);
    }
}
