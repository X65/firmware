/*
 * Copyright (c) 2023 Rumbledethumps
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mem.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/vreg.h"
#include "main.h"
#include "mem.pio.h"
#include "pico/stdlib.h"
#include <stdbool.h>
#include <stdint.h>

#define MEM_BUS_PIO_CLKDIV_INT 10

#ifdef NDEBUG
uint8_t ram[0x10000];
#else
static struct
{
    uint8_t _0[0x1000];
    uint8_t _1[0x1000];
    uint8_t _2[0x1000];
    uint8_t _3[0x1000];
    uint8_t _4[0x1000];
    uint8_t _5[0x1000];
    uint8_t _6[0x1000];
    uint8_t _7[0x1000];
    uint8_t _8[0x1000];
    uint8_t _9[0x1000];
    uint8_t _A[0x1000];
    uint8_t _B[0x1000];
    uint8_t _C[0x1000];
    uint8_t _D[0x1000];
    uint8_t _E[0x1000];
    uint8_t _F[0x1000];
    // this struct of 4KB segments is because
    // a single 64KB array crashes my debugger
} ram_blocks;
uint8_t *const ram = (uint8_t *)&ram_blocks;
#endif

uint8_t mbuf[MBUF_SIZE] __attribute__((aligned(4)));
size_t mbuf_len;

int mem_read_chan;
int mem_write_chan;

static void mem_bus_pio_init(void)
{
    // PIO to manage PHI2 clock and 65816 address/data bus
    uint offset = pio_add_program(MEM_BUS_PIO, &mem_bus_program);
    pio_sm_config config = mem_bus_program_get_default_config(offset);
    sm_config_set_clkdiv_int_frac(&config, MEM_BUS_PIO_CLKDIV_INT, 0); // FIXME: remove?
    sm_config_set_in_shift(&config, true, true, 32);
    sm_config_set_out_shift(&config, true, false, 0);
    sm_config_set_sideset_pins(&config, MEM_CTL_PIN_BASE);
    sm_config_set_in_pins(&config, MEM_BUS_PIN_BASE);
    sm_config_set_out_pins(&config, MEM_DATA_PIN_BASE, 8);
    for (int i = MEM_BUS_PIN_BASE; i < MEM_BUS_PIN_BASE + MEM_BUS_PINS_USED; i++)
        pio_gpio_init(MEM_BUS_PIO, i);
    for (int i = MEM_CTL_PIN_BASE; i < MEM_CTL_PIN_BASE + MEM_CTL_PINS_USED; i++)
        pio_gpio_init(MEM_BUS_PIO, i);
    pio_sm_set_consecutive_pindirs(MEM_BUS_PIO, MEM_BUS_SM, MEM_BUS_PIN_BASE, MEM_BUS_PINS_USED, false);
    pio_sm_set_consecutive_pindirs(MEM_BUS_PIO, MEM_BUS_SM, MEM_CTL_PIN_BASE, MEM_CTL_PINS_USED, true);
    gpio_pull_up(CPU_PHI2_PIN);
    pio_sm_init(MEM_BUS_PIO, MEM_BUS_SM, offset, &config);
    pio_sm_set_enabled(MEM_BUS_PIO, MEM_BUS_SM, true);
}

static void mem_dma_init(void)
{
    mem_read_chan = dma_claim_unused_channel(true);
    dma_channel_config read_dma = dma_channel_get_default_config(mem_read_chan);
    channel_config_set_high_priority(&read_dma, true);
    // channel_config_set_dreq(&read_dma, pio_get_dreq(MEM_RAM_PIO, MEM_RAM_READ_SM, false));
    channel_config_set_read_increment(&read_dma, false);
    channel_config_set_transfer_data_size(&read_dma, DMA_SIZE_8);
    dma_channel_configure(
        mem_read_chan,
        &read_dma,
        &MEM_BUS_PIO->txf[MEM_BUS_SM], // dst
        mbuf,                          // src
        1,
        false);

    mem_write_chan = dma_claim_unused_channel(true);
    dma_channel_config write_dma = dma_channel_get_default_config(mem_write_chan);
    channel_config_set_high_priority(&write_dma, true);
    // channel_config_set_dreq(&write_dma, pio_get_dreq(MEM_BUS_PIO, MEM_BUS_SM, false));
    channel_config_set_read_increment(&write_dma, false);
    channel_config_set_transfer_data_size(&write_dma, DMA_SIZE_8);
    dma_channel_configure(
        mem_write_chan,
        &write_dma,
        mbuf,                          // dst
        &MEM_BUS_PIO->rxf[MEM_BUS_SM], // src
        1,
        false);
}

void mem_init(void)
{
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(10);
    set_sys_clock_khz(266000, true);
    main_reclock();

    // Adjustments for GPIO performance. Important!
    for (int i = MEM_BUS_PIN_BASE; i < MEM_BUS_PIN_BASE + MEM_BUS_PINS_USED; ++i)
    {
        gpio_set_pulls(i, true, true);
        gpio_set_input_hysteresis_enabled(i, false);
        hw_set_bits(&MEM_BUS_PIO->input_sync_bypass, 1u << i);
    }
    for (int i = MEM_CTL_PIN_BASE; i < MEM_CTL_PIN_BASE + MEM_CTL_PINS_USED; ++i)
    {
        gpio_set_pulls(i, true, true);
        gpio_set_input_hysteresis_enabled(i, false);
        hw_set_bits(&MEM_BUS_PIO->input_sync_bypass, 1u << i);
    }

    // the inits
    mem_bus_pio_init();
    mem_dma_init();
}

void mem_run(void)
{
    // TODO: connect bus PIO and mem PIO with DMA channels
}

void mem_stop(void)
{
    // TODO: remove above connection
}

extern void dump_cpu_history(void);

void mem_task(void)
{
    // dump_cpu_history();
}

void mem_read_buf(uint32_t addr)
{
    if ((addr & 0xFFFFE0) == 0x00FFE0)
    {
        for (size_t i = 0; i < mbuf_len; ++i)
        {
            mbuf[i] = REGS(addr++);
        }
    }
    else
    {
        int dma_chan = dma_claim_unused_channel(true);
        dma_channel_config dma_chan_config = dma_channel_get_default_config(dma_chan);
        channel_config_set_transfer_data_size(&dma_chan_config, DMA_SIZE_8);
        channel_config_set_read_increment(&dma_chan_config, true);
        channel_config_set_write_increment(&dma_chan_config, true);
        dma_channel_configure(
            dma_chan,
            &dma_chan_config,
            mbuf,       // Write to buffer
            &ram[addr], // Read values from "RAM"
            mbuf_len,   // mbuf_len values to copy
            true        // Start immediately
        );
        dma_channel_wait_for_finish_blocking(dma_chan);
        dma_channel_unclaim(dma_chan);
    }
}

void mem_write_buf(uint32_t addr)
{
    if ((addr & 0xFFFFE0) == 0x00FFE0)
    {
        for (size_t i = 0; i < mbuf_len; ++i)
        {
            REGS(addr++) = mbuf[i];
        }
    }
    else
    {
        assert(mbuf_len <= 32); // Memory Write wraps at 32 bytes

        int dma_chan = dma_claim_unused_channel(true);
        dma_channel_config dma_chan_config = dma_channel_get_default_config(dma_chan);
        channel_config_set_transfer_data_size(&dma_chan_config, DMA_SIZE_8);
        channel_config_set_read_increment(&dma_chan_config, true);
        channel_config_set_write_increment(&dma_chan_config, true);
        dma_channel_configure(
            dma_chan,
            &dma_chan_config,
            &ram[addr], // Write to "RAM"
            mbuf,       // Read values from buffer
            mbuf_len,   // mbuf_len values to copy
            true        // Start immediately
        );
        dma_channel_wait_for_finish_blocking(dma_chan);
        dma_channel_unclaim(dma_chan);
    }
}

void mem_print_status(void)
{
    // TODO: get memory status from VPU, print
}
