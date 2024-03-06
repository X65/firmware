/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/ria.h"
#include "hardware/structs/bus_ctrl.h"
#include "littlefs/lfs_util.h"
#include "main.h"
#include "pico/stdlib.h"
#include "sys/mem.h"

#define RIA_WATCHDOG_MS 250

static volatile bool irq_enabled;

void ria_trigger_irq(void)
{
    if (irq_enabled & 0x01)
        gpio_put(CPU_IRQB_PIN, false);
}

uint32_t ria_buf_crc32(void)
{
    // use littlefs library
    return ~lfs_crc(~0, mbuf, mbuf_len);
}

void ria_run(void)
{
}

void ria_stop(void)
{
    irq_enabled = false;
    gpio_put(CPU_IRQB_PIN, true);
}

void ria_task(void)
{
}

void ria_init(void)
{
    // drive irq pin
    gpio_init(CPU_IRQB_PIN);
    gpio_put(CPU_IRQB_PIN, true);
    gpio_set_dir(CPU_IRQB_PIN, true);

    // safety check for compiler alignment
    assert(!((uintptr_t)regs & 0x1F));

    // Lower CPU0 on crossbar by raising others
    bus_ctrl_hw->priority |=              //
        BUSCTRL_BUS_PRIORITY_DMA_R_BITS | //
        BUSCTRL_BUS_PRIORITY_DMA_W_BITS | //
        BUSCTRL_BUS_PRIORITY_PROC1_BITS;
}
