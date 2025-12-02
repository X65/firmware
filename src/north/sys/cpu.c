/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/cpu.h"
#include "cpu.pio.h"
#include "hw.h"
#include <hardware/clocks.h>
#include <hardware/pio.h>
#include <pico/stdlib.h>
#include <stdio.h>

#if defined(DEBUG_RIA_SYS) || defined(DEBUG_RIA_SYS_CPU)
#include <stdio.h>
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
static inline void DBG(const char *fmt, ...)
{
    (void)fmt;
}
#endif

static bool cpu_run_requested;
static absolute_time_t cpu_resb_timer;

void cpu_task(void)
{
    // Enforce minimum RESB time
    if (cpu_run_requested && !gpio_get(CPU_RESB_PIN))
    {
        absolute_time_t now = get_absolute_time();
        if (absolute_time_diff_us(now, cpu_resb_timer) < 0)
            gpio_put(CPU_RESB_PIN, true);
    }
}

void cpu_run(void)
{
    cpu_run_requested = true;
}

void cpu_stop(void)
{
    cpu_run_requested = false;
    gpio_put(CPU_RESB_PIN, false);
    cpu_resb_timer = delayed_by_us(get_absolute_time(),
                                   cpu_get_reset_us());
}

bool cpu_active(void)
{
    return cpu_run_requested;
}

uint32_t cpu_get_reset_us(void)
{
#ifndef RP6502_RESB_US
#define RP6502_RESB_US 0
#endif
    // If provided, use RP6502_RESB_US unless PHI2
    // speed needs longer for 2 clock cycles.
    // One extra microsecond to get ceil.
    uint32_t reset_us = 2000 / CPU_PHI2_KHZ + 1;
    if (!RP6502_RESB_US)
        return reset_us;
    return RP6502_RESB_US < reset_us
               ? reset_us
               : RP6502_RESB_US;
}

static void cpu_bus_pio_init(void)
{
    // PIO to manage PHI2 clock and memory bridge
    uint offset = pio_add_program(CPU_BUS_PIO, &cpu_bus_program);
    pio_sm_config config = cpu_bus_program_get_default_config(offset);
    const float clkdiv = (float)(clock_get_hz(clk_sys)) / (CPU_BUS_PIO_SPEED_KHZ * KHZ);
    sm_config_set_clkdiv(&config, clkdiv);
    sm_config_set_in_shift(&config, true, true, 32);
    sm_config_set_out_shift(&config, true, false, 0);
    sm_config_set_sideset_pins(&config, CPU_CTL_PIN_BASE);
    sm_config_set_in_pins(&config, CPU_BUS_PIN_BASE);
    sm_config_set_in_pin_count(&config, CPU_BUS_PINS_USED);
    sm_config_set_out_pins(&config, CPU_DATA_PIN_BASE, 8);
    for (int i = CPU_BUS_PIN_BASE; i < CPU_BUS_PIN_BASE + CPU_BUS_PINS_USED; i++)
        pio_gpio_init(CPU_BUS_PIO, i % 32);
    for (int i = CPU_CTL_PIN_BASE; i < CPU_CTL_PIN_BASE + CPU_CTL_PINS_USED; i++)
        pio_gpio_init(CPU_BUS_PIO, i);
    pio_sm_set_consecutive_pindirs(CPU_BUS_PIO, CPU_BUS_SM, CPU_BUS_PIN_BASE, CPU_BUS_PINS_USED, false);
    pio_sm_set_consecutive_pindirs(CPU_BUS_PIO, CPU_BUS_SM, CPU_CTL_PIN_BASE, CPU_CTL_PINS_USED, true);
    // pio_set_irq1_source_enabled(CPU_BUS_PIO, pis_sm0_rx_fifo_not_empty, true);
    // pio_interrupt_clear(CPU_BUS_PIO, MEM_BUS_PIO_IRQ);
    pio_sm_init(CPU_BUS_PIO, CPU_BUS_SM, offset, &config);
    // irq_set_exclusive_handler(PIO_IRQ_NUM(CPU_BUS_PIO, MEM_BUS_PIO_IRQ), mem_bus_pio_irq_handler);
    // irq_set_enabled(PIO_IRQ_NUM(CPU_BUS_PIO, MEM_BUS_PIO_IRQ), true);
    CPU_BUS_PIO->irq = (1u << STALL_IRQ); // clear gating IRQ
    pio_sm_set_enabled(CPU_BUS_PIO, CPU_BUS_SM, true);
}

void cpu_init(void)
{
    // drive reset pin
    gpio_init(CPU_RESB_PIN);
    gpio_put(CPU_RESB_PIN, false);
    gpio_set_dir(CPU_RESB_PIN, true);

    if (!gpio_get(CPU_RESB_PIN))
        cpu_resb_timer = delayed_by_us(get_absolute_time(), cpu_get_reset_us());

    // Adjustments for GPIO performance. Important!
    for (int i = CPU_BUS_PIN_BASE; i < CPU_BUS_PIN_BASE + CPU_BUS_PINS_USED; i++)
    {
        int idx = i % 32;
        pio_gpio_init(CPU_BUS_PIO, idx);
        gpio_set_pulls(idx, false, false);
        gpio_set_input_hysteresis_enabled(idx, false);
        hw_set_bits(&CPU_BUS_PIO->input_sync_bypass, 1u << idx);
    }

    // PIO init
    cpu_bus_pio_init();
}

void cpu_print_status(void)
{
    printf("CPU : ~%.2fMHz\n", (float)CPU_BUS_PIO_SPEED_KHZ / 12 / KHZ);
}
