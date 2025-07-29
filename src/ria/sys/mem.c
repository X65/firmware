/*
 * Copyright (c) 2023 Rumbledethumps
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mem.h"
#include "littlefs/lfs_util.h"
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

uint8_t xstack[XSTACK_SIZE + 1];
size_t volatile xstack_ptr;

uint8_t mbuf[MBUF_SIZE] __attribute__((aligned(4)));
size_t mbuf_len;

inline uint32_t mbuf_crc32(void)
{
    // use littlefs library
    return ~lfs_crc(~0, mbuf, mbuf_len);
}

void mem_init(void)
{
    psram_size = setup_psram(QMI_PSRAM_CS_PIN);
}

void mem_post_reclock(void)
{
    set_psram_timing();
}

void mem_read_buf(uint32_t addr)
{
    for (size_t i = 0; i < mbuf_len; ++i, ++addr)
    {
        if (addr >= 0xFFC0 && addr < 0x10000)
        {
            mbuf[i] = REGS(addr);
        }
        else
        {
            mbuf[i] = psram[addr & 0xFFFFFF];
        }
    }
}

void mem_write_buf(uint32_t addr)
{
    for (size_t i = 0; i < mbuf_len; ++i, ++addr)
    {
        if (addr >= 0xFFC0 && addr < 0x10000)
        {
            REGS(addr) = mbuf[i];
        }
        else
        {
            psram[addr & 0xFFFFFF] = mbuf[i];
        }
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
