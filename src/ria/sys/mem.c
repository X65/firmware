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

void mem_reclock(void)
{
    set_psram_timing();
}

void mem_read_buf(uint32_t addr)
{
    for (size_t i = 0; i < mbuf_len; ++i)
    {
        if ((addr & 0xFFFFE0) == 0x00FFE0
            && (addr & 0xFF) != 0xE4 // COP_L
            && (addr & 0xFF) != 0xE5 // COP_H
            && (addr & 0xFF) != 0xE6 // BRK_L
            && (addr & 0xFF) != 0xE7 // BRK_H
            && (addr & 0xFF) != 0xE8 // ABORTB_L
            && (addr & 0xFF) != 0xE9 // ABORTB_H
            && (addr & 0xFF) != 0xEA // NMIB_L
            && (addr & 0xFF) != 0xEB // NMIB_H
            && (addr & 0xFF) != 0xEE // IRQB_L
            && (addr & 0xFF) != 0xEF // IRQB_H
            && (addr & 0xFF) != 0xF4 // COP_L
            && (addr & 0xFF) != 0xF5 // COP_H
            && (addr & 0xFF) != 0xF8 // ABORTB_L
            && (addr & 0xFF) != 0xF9 // ABORTB_H
            && (addr & 0xFF) != 0xFA // NMIB_L
            && (addr & 0xFF) != 0xFB // NMIB_H
            && (addr & 0xFF) != 0xFC // RESETB_l
            && (addr & 0xFF) != 0xFD // RESETB_H
            && (addr & 0xFF) != 0xFE // IRQB/BRK_L
            && (addr & 0xFF) != 0xFF // IRQB/BRK_H
        )
        {
            mbuf[i] = REGS(addr++);
        }
        else
        {
            mbuf[i] = psram[addr++ & 0xFFFFFF];
        }
    }
}

void mem_write_buf(uint32_t addr)
{
    for (size_t i = 0; i < mbuf_len; ++i)
    {
        if ((addr & 0xFFFFE0) == 0x00FFE0
            && (addr & 0xFF) != 0xE4 // COP_L
            && (addr & 0xFF) != 0xE5 // COP_H
            && (addr & 0xFF) != 0xE6 // BRK_L
            && (addr & 0xFF) != 0xE7 // BRK_H
            && (addr & 0xFF) != 0xE8 // ABORTB_L
            && (addr & 0xFF) != 0xE9 // ABORTB_H
            && (addr & 0xFF) != 0xEA // NMIB_L
            && (addr & 0xFF) != 0xEB // NMIB_H
            && (addr & 0xFF) != 0xEE // IRQB_L
            && (addr & 0xFF) != 0xEF // IRQB_H
            && (addr & 0xFF) != 0xF4 // COP_L
            && (addr & 0xFF) != 0xF5 // COP_H
            && (addr & 0xFF) != 0xF8 // ABORTB_L
            && (addr & 0xFF) != 0xF9 // ABORTB_H
            && (addr & 0xFF) != 0xFA // NMIB_L
            && (addr & 0xFF) != 0xFB // NMIB_H
            && (addr & 0xFF) != 0xFC // RESETB_l
            && (addr & 0xFF) != 0xFD // RESETB_H
            && (addr & 0xFF) != 0xFE // IRQB/BRK_L
            && (addr & 0xFF) != 0xFF // IRQB/BRK_H
        )
        {
            REGS(addr++) = mbuf[i];
        }
        else
        {
            psram[addr++ & 0xFFFFFF] = mbuf[i];
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
