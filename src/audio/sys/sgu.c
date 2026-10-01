#include "./sgu.h"
#include "hw.h"
#include "sys/led.h"
#include <hardware/irq.h>
#include <hardware/pio.h>
#include <hardware/structs/sio.h>
#include <math.h>
#include <pico/multicore.h>
#include <pico/rand.h>
#include <pico/unique_id.h>
#include <pico/time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Channel split: core 1 does 0..(SGU_CORE1_CHNS-1), core 0 does SGU_CORE1_CHNS..(SGU_CHNS-1)
#define SGU_CORE1_CHNS 5

sgu1_t sgu_instance;
#define SGU (&sgu_instance)

// PCM sample memory, never cleared by a reset
static int8_t __uninitialized_ram(pcm_mem)[SGU1_PCM_BANKS * SGU_PCM_BANK_SIZE];

static const uint8_t svc_magic[] = {'S', 'G', 'U', '1'};
// the board's unique ID - the emulator reads all zeroes
static uint8_t svc_unique_id[SGU1_SVC_UNIQUE_ID_LEN];

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

__force_inline static inline int16_t __attribute__((optimize("O2")))
clamp(int32_t sample)
{
    return (int16_t)__builtin_arm_ssat(sample, 16);
}

// Core 0 FIFO IRQ handler — computes channels SGU_CORE1_CHNS..(SGU_CHNS-1)
// and pushes partial L,R sums back to core 1 via FIFO.
static void __isr __not_in_flash_func(core0_audio_isr)(void)
{
    // Drain the "go" signal and clear IRQ
    while (multicore_fifo_rvalid())
        (void)sio_hw->fifo_rd;
    multicore_fifo_clear_irq();

    // Compute channels SGU_CORE1_CHNS .. SGU_CHNS-1
    int32_t l = 0, r = 0;
    SGU_NextSample_Channels(&SGU->sgu, SGU_CORE1_CHNS, SGU_CHNS, &l, &r);

    // Send partial sums to core 1
    multicore_fifo_push_blocking_inline((uint32_t)l);
    multicore_fifo_push_blocking_inline((uint32_t)r);
}

__force_inline static inline void __attribute__((optimize("O2")))
_sgu_tick(void)
{
    // 1. Global setup (LFO, envelope counters)
    SGU_NextSample_Setup(&SGU->sgu);

    // 2. Signal core 0 to start its channel subset
    multicore_fifo_push_blocking_inline(1);

    // 3. Compute channels 0..(SGU_CORE1_CHNS-1) on this core
    int32_t l1 = 0, r1 = 0;
    SGU_NextSample_Channels(&SGU->sgu, 0, SGU_CORE1_CHNS, &l1, &r1);

    // 4. Wait for core 0's partial sums
    int32_t l0 = (int32_t)multicore_fifo_pop_blocking_inline();
    int32_t r0 = (int32_t)multicore_fifo_pop_blocking_inline();

    // 5. Merge and finalize (DC-removal HPF)
    int32_t l, r;
    SGU_NextSample_Finalize(&SGU->sgu, (int64_t)l1 + l0, (int64_t)r1 + r0, &l, &r);

    // Final mixer gain, linear value / 255: $FF is unity, $00 mute
    const uint8_t vol = SGU->svc_master_vol;
    if (vol != 0xFF)
    {
        l = (int32_t)((int64_t)l * vol / 0xFF);
        r = (int32_t)((int64_t)r * vol / 0xFF);
    }

    const int16_t left = clamp(l >> 1);
    const int16_t right = clamp(r >> 1);
    SGU->sample = ((uint32_t)(uint16_t)left << 16) | (uint16_t)right;
}

__attribute__((optimize("O2"))) static void __no_inline_not_in_flash_func(sgu_loop)(void)
{
    while (true)
    {
        while (!pio_interrupt_get(AUD_I2S_PIO, AUD_PIO_IRQ))
        {
            tight_loop_contents();
        }
        pio_interrupt_clear(AUD_I2S_PIO, AUD_PIO_IRQ);

        static uint32_t tick_start, tick_elapsed;
        tick_start = time_us_32();
        _sgu_tick();
        tick_elapsed = time_us_32() - tick_start;

        pio_sm_put_blocking(AUD_I2S_PIO, AUD_I2S_SM, SGU->sample);
        led_blink_color((SGU->sample >> 4) | 0x441122);

        // check whether generation managed frame timing constraint
        if (pio_interrupt_get(AUD_I2S_PIO, AUD_PIO_IRQ))
        {
            printf("SGU tick overrun!\nTook %lu us\n", tick_elapsed);
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
    SGU_Init(&SGU->sgu, pcm_mem, sizeof(pcm_mem));

    static_assert(sizeof(pico_unique_board_id_t) == SGU1_SVC_UNIQUE_ID_LEN);
    pico_get_unique_board_id((pico_unique_board_id_t *)svc_unique_id);

    // Register core 0 FIFO IRQ handler for dual-core audio rendering
    irq_set_exclusive_handler(SIO_IRQ_FIFO, core0_audio_isr);
    irq_set_priority(SIO_IRQ_FIFO, 0); // highest priority — preempts SPI
    irq_set_enabled(SIO_IRQ_FIFO, true);

    printf("Starting SGU core...\n");
    multicore_launch_core1(sgu_loop);
}

// Service registers. The channel select is left alone, so a CHIP_RESET
// sequence keeps the service bank selected.
static void sgu_service_reset(void)
{
    SGU->svc_sample_offset = 0;
    SGU->svc_sample_bank = 0;
    SGU->svc_master_vol = 0;
}

void sgu_reset()
{
    // core 1 may be rendering - the core resets at the next sample boundary
    SGU_RequestReset(&SGU->sgu, SGU_RESET_ALL);
    (void)SGU_GetFlags(&SGU->sgu);
    SGU->svc_status = 0;
    SGU->sample = 0;
    SGU->selected_channel = 0;
    sgu_service_reset();
}

static uint8_t *sgu_service_pcm(void)
{
    const size_t address = (size_t)SGU->svc_sample_bank * SGU_PCM_BANK_SIZE + SGU->svc_sample_offset;
    return address < sizeof(pcm_mem) ? (uint8_t *)&pcm_mem[address] : NULL;
}

static uint8_t sgu_service_read(uint8_t reg)
{
    if (reg <= SGU1_SVC_MAGIC_END)
        return svc_magic[reg - SGU1_SVC_MAGIC];
    if (reg >= SGU1_SVC_UNIQUE_ID && reg <= SGU1_SVC_UNIQUE_ID_END)
        return svc_unique_id[reg - SGU1_SVC_UNIQUE_ID];

    switch (reg)
    {
    case SGU1_SVC_VER_MAJOR:
        return SGU1_VERSION_MAJOR;
    case SGU1_SVC_VER_MINOR:
        return SGU1_VERSION_MINOR;
    case SGU1_SVC_PCM_BANKS:
        return SGU1_PCM_BANKS;
    case SGU1_SVC_SVC_BANKS:
        return 0;
    case SGU1_SVC_STATUS:
    {
        // read-to-clear, only the bits returned
        SGU->svc_status |= SGU_GetFlags(&SGU->sgu);
        const uint8_t data = (uint8_t)SGU->svc_status;
        SGU->svc_status &= ~(uint32_t)data;
        return data;
    }
    case SGU1_SVC_SAMPLE_OFF_LO:
        return (uint8_t)SGU->svc_sample_offset;
    case SGU1_SVC_SAMPLE_OFF_HI:
        return (uint8_t)(SGU->svc_sample_offset >> 8);
    case SGU1_SVC_SAMPLE_BANK:
        return SGU->svc_sample_bank;
    case SGU1_SVC_SAMPLE_DATA:
    {
        // unbacked banks read zero, the offset advances anyway
        const uint8_t *pcm = sgu_service_pcm();
        SGU->svc_sample_offset++;
        return pcm ? *pcm : 0;
    }
    case SGU1_SVC_MASTER_VOL:
        return SGU->svc_master_vol;
    default:
        return 0;
    }
}

static void sgu_service_chip_reset(uint8_t data)
{
    if ((data & SGU1_RESET_MAGIC_MASK) != SGU1_RESET_MAGIC)
        return;

    uint32_t parts = 0;
    if (data & SGU1_RESET_VOICES)
        parts |= SGU_RESET_VOICES;
    if (data & SGU1_RESET_TIMEBASE)
        parts |= SGU_RESET_TIMEBASE;
    if (data & SGU1_RESET_MIX)
    {
        parts |= SGU_RESET_MIX;
        SGU->svc_status = 0;
    }
    // core 1 may be rendering - the core resets at the next sample boundary
    if (parts)
        SGU_RequestReset(&SGU->sgu, parts);
    if (data & SGU1_RESET_SVC)
        sgu_service_reset();
}

static void sgu_service_write(uint8_t reg, uint8_t data)
{
    switch (reg)
    {
    case SGU1_SVC_CHIP_RESET:
        sgu_service_chip_reset(data);
        break;
    case SGU1_SVC_SAMPLE_OFF_LO:
        SGU->svc_sample_offset = (SGU->svc_sample_offset & 0xFF00u) | data;
        break;
    case SGU1_SVC_SAMPLE_OFF_HI:
        SGU->svc_sample_offset = (uint16_t)((SGU->svc_sample_offset & 0x00FFu) | ((uint16_t)data << 8));
        break;
    case SGU1_SVC_SAMPLE_BANK:
        SGU->svc_sample_bank = data;
        break;
    case SGU1_SVC_SAMPLE_DATA:
    {
        // unbacked banks discard writes, the offset advances anyway
        uint8_t *pcm = sgu_service_pcm();
        if (pcm)
            *pcm = data;
        SGU->svc_sample_offset++;
        break;
    }
    case SGU1_SVC_MASTER_VOL:
        SGU->svc_master_vol = data;
        break;
    default:
        // identification, STATUS and reserved registers ignore writes
        break;
    }
}

uint8_t sgu_reg_read(uint8_t reg)
{
    reg &= SGU_REGS_PER_CH - 1;
    if (reg == SGU_REGS_PER_CH - 1)
        return SGU->selected_channel;
    if (SGU->selected_channel < SGU_CHNS)
        // the core applies the FLAGS1 DIAG readback
        return SGU_RegRead(&SGU->sgu, SGU->selected_channel, reg);
    if (SGU->selected_channel == SGU1_SERVICE_BANK)
        return sgu_service_read(reg);
    // reserved bank
    return 0xFF;
}

void sgu_reg_write(uint8_t reg, uint8_t data)
{
    reg &= SGU_REGS_PER_CH - 1;
    if (reg == SGU_REGS_PER_CH - 1)
        SGU->selected_channel = data;
    else if (SGU->selected_channel < SGU_CHNS)
        SGU_Write(&SGU->sgu, (uint16_t)(SGU->selected_channel << 6) | reg, data);
    else if (SGU->selected_channel == SGU1_SERVICE_BANK)
        sgu_service_write(reg, data);
    // reserved banks ignore writes
}
