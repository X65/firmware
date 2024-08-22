/*
 * Copyright (c) 2023 Rumbledethumps
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mem.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "main.h"
#include <stdbool.h>
#include <stdint.h>

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
    // the inits
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

void mem_task(void)
{
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
