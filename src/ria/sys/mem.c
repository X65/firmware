/*
 * Copyright (c) 2023 Rumbledethumps
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mem.h"
#include "hardware/dma.h"
#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static size_t psram_size = 0;

/// @brief The setup_psram function - note that this is not in flash
extern size_t setup_psram(uint32_t psram_cs_pin);

/// @brief The sfe_psram_update_timing function - note that this is not in flash
/// @note - updates the PSRAM QSPI timing - call if the system clock is changed after PSRAM is initialized
extern void set_psram_timing(void);

uint8_t mbuf[MBUF_SIZE] __attribute__((aligned(4)));
size_t mbuf_len;

void mem_init(void)
{
    psram_size = setup_psram(QMI_PSRAM_CS_PIN);
}

void mem_reclock(void)
{
    set_psram_timing();
}

void mem_run(void)
{
}

void mem_stop(void)
{
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
            mbuf,         // Write to buffer
            &psram[addr], // Read values from "RAM"
            mbuf_len,     // mbuf_len values to copy
            true          // Start immediately
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
            &psram[addr], // Write to "RAM"
            mbuf,         // Read values from buffer
            mbuf_len,     // mbuf_len values to copy
            true          // Start immediately
        );
        dma_channel_wait_for_finish_blocking(dma_chan);
        dma_channel_unclaim(dma_chan);
    }
}

void mem_print_status(void)
{

    if (psram_size == 0)
    {
        printf("RAM not detected\n");
    }
    else
    {
        printf("RAM: %dMB\n", psram_size / (1024 * 1024));
    }
}
