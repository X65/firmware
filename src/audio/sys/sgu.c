#include "./sgu.h"
#include "hw.h"
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

volatile int16_t __uninitialized_ram(tanh_lut)[INT16_MAX + 1]
    __attribute__((aligned(4)));

// Tuning: Lower = Louder/Crunchier, Higher = Cleaner.
// Try values between 2000.0 and 3500.0.
static const double V_REF = 2800.0;

static void sgu_init_tanh_lut()
{

    for (size_t i = 0; i <= INT16_MAX; i++)
    {
        // i / V_REF is the input to tanh
        double x = (double)i / V_REF;
        double y = tanh(x);

        // Scale back to 16-bit integer range
        tanh_lut[i] = (int16_t)(y * INT16_MAX);
    }
}

static inline int16_t __force_inline __attribute__((optimize("O3")))
soft_clip_int32(int32_t sample)
{
    // Absolute value for LUT lookup
    int32_t abs_s = (sample < 0) ? -sample : sample;

    // Safety Clamp to LUT range
    if (abs_s > INT16_MAX)
        abs_s = INT16_MAX;

    // Lookup with sign restoration
    int16_t saturated = tanh_lut[abs_s];
    return (sample < 0) ? -saturated : saturated;
}

static inline void __force_inline __attribute__((optimize("O3")))
_sgu_tick(void)
{
    int32_t l, r;
    SGU_NextSample(&SGU->sgu, &l, &r);

    // Gain then Saturate+Clip
    ((int16_t *)(&SGU->sample))[1] = soft_clip_int32(l << 2);
    ((int16_t *)(&SGU->sample))[0] = soft_clip_int32(r << 2);
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
    sgu_init_tanh_lut();

    memset(SGU, 0, sizeof(*SGU));
    SGU_Init(&SGU->sgu, SGU_PCM_RAM_SIZE);

    printf("Starting SGU core...\n");
    multicore_launch_core1(sgu_loop);
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
        data = ((unsigned char *)SGU->sgu.chan)[(SGU->selected_channel % SGU_CHNS) << 6 | (reg & (SGU_REGS_PER_CH - 1))];
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
        // ((unsigned char*)SGU->sgu.chan)[(SGU->selected_channel % SGU_CHNS) << 6 | (reg & (SGU_REGS_PER_CH - 1))] =
        // data;
        SGU_Write(&SGU->sgu, (uint16_t)((SGU->selected_channel % SGU_CHNS) << 6) | (reg & (SGU_REGS_PER_CH - 1)), data);
    }
}
