/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/cpu.h"
#include "api/api.h"
#include "cpu.pio.h"
#include "hw.h"
#include "sys/cfg.h"
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

bool cpu_api_phi2(void)
{
    return api_return_ax(cfg_get_phi2_khz());
}

uint32_t cpu_get_reset_us(void)
{
#ifndef RP6502_RESB_US
#define RP6502_RESB_US 0
#endif
    // If provided, use RP6502_RESB_US unless PHI2
    // speed needs longer for 2 clock cycles.
    // One extra microsecond to get ceil.
    uint32_t reset_us = 2000 / cfg_get_phi2_khz() + 1;
    if (!RP6502_RESB_US)
        return reset_us;
    return RP6502_RESB_US < reset_us
               ? reset_us
               : RP6502_RESB_US;
}

static float cpu_compute_phi2_hz(uint32_t clk_sys_hz,
                                 uint32_t clkdiv_int,
                                 uint8_t clkdiv_frac)
{
    float clkdiv = (float)clkdiv_int + (float)clkdiv_frac / 256.0f;
    float cpu_bus_hz = (float)clk_sys_hz / clkdiv;
    return cpu_bus_hz / (cpu_bus_TICKS * 2);
}

uint32_t cpu_validate_phi2_khz(uint32_t freq_khz)
{
    if (!freq_khz)
        freq_khz = CPU_PHI2_DEFAULT;
    if (freq_khz < CPU_PHI2_MIN_KHZ)
        freq_khz = CPU_PHI2_MIN_KHZ;
    if (freq_khz > CPU_PHI2_MAX_KHZ)
        freq_khz = CPU_PHI2_MAX_KHZ;

    uint32_t clk_sys_hz = clock_get_hz(clk_sys);
    float clkdiv = (float)(clk_sys_hz) / (float)(freq_khz * KHZ * cpu_bus_TICKS * 2);
    uint32_t clkdiv_int;
    uint8_t clkdiv_frac;
    pio_calculate_clkdiv8_from_float(clkdiv, &clkdiv_int, &clkdiv_frac);

    return (uint32_t)(cpu_compute_phi2_hz(clk_sys_hz, clkdiv_int, clkdiv_frac) / KHZ);
}

static float cpu_get_phi2_hz(void)
{
    uint32_t clkdiv_raw = CPU_BUS_PIO->sm[CPU_BUS_SM].clkdiv;
    uint32_t clkdiv_int = clkdiv_raw >> 16;
    uint8_t clkdiv_frac = (clkdiv_raw >> 8) & 0xFF;

    return cpu_compute_phi2_hz(clock_get_hz(clk_sys), clkdiv_int, clkdiv_frac);
}

bool cpu_set_phi2_khz(uint32_t phi2_khz)
{
    const float clkdiv = (float)(clock_get_hz(clk_sys)) / (float)(phi2_khz * KHZ * cpu_bus_TICKS * 2);
    pio_sm_set_clkdiv(CPU_BUS_PIO, CPU_BUS_SM, clkdiv);
    return true;
}

static void cpu_bus_pio_init(void)
{
    // PIO to manage PHI2 clock and memory bridge
    pio_sm_claim(CPU_BUS_PIO, CPU_BUS_SM);
    uint offset = pio_add_program(CPU_BUS_PIO, &cpu_bus_program);
    pio_sm_config config = cpu_bus_program_get_default_config(offset);
    sm_config_set_clkdiv(&config, 100); // temporary, will be reclocked later
    sm_config_set_in_shift(&config, true, true, 32);
    sm_config_set_out_shift(&config, true, false, 0);
    sm_config_set_sideset_pins(&config, CPU_CTL_PIN_BASE);
    sm_config_set_in_pins(&config, CPU_BUS_PIN_BASE);
    sm_config_set_in_pin_count(&config, CPU_BUS_PINS_USED);
    sm_config_set_out_pins(&config, CPU_DATA_PIN_BASE, 8);
    for (int i = CPU_BUS_PIN_BASE; i < CPU_BUS_PIN_BASE + CPU_BUS_PINS_USED; i++)
        pio_gpio_init(CPU_BUS_PIO, i % 32);
    for (int i = CPU_CTL_PIN_BASE; i < CPU_CTL_PIN_BASE + CPU_CTL_PINS_USED; i++)
        pio_gpio_init(CPU_BUS_PIO, i % 32);
    pio_sm_set_consecutive_pindirs(CPU_BUS_PIO, CPU_BUS_SM, CPU_BUS_PIN_BASE, CPU_BUS_PINS_USED, false);
    pio_sm_set_consecutive_pindirs(CPU_BUS_PIO, CPU_BUS_SM, CPU_CTL_PIN_BASE, CPU_CTL_PINS_USED, true);
    pio_sm_init(CPU_BUS_PIO, CPU_BUS_SM, offset + cpu_bus_offset_start, &config);
    CPU_BUS_PIO->irq = (1u << cpu_bus_SIRQ); // clear gating IRQ
    pio_sm_set_enabled(CPU_BUS_PIO, CPU_BUS_SM, true);
}

static void cpu_gpio_pin_init(int idx)
{
    int i = idx % 32;
    gpio_set_pulls(i, false, false);
    gpio_set_input_hysteresis_enabled(i, false);
    hw_set_bits(&CPU_BUS_PIO->input_sync_bypass, 1u << i);
}

void cpu_init(void)
{
    // drive reset pin
    gpio_init(CPU_RESB_PIN);
    gpio_put(CPU_RESB_PIN, false);
    gpio_set_dir(CPU_RESB_PIN, true);

    // PIO init
    cpu_bus_pio_init();

    // Adjustments for GPIO performance. Important!
    for (int i = CPU_BUS_PIN_BASE; i < CPU_BUS_PIN_BASE + CPU_BUS_PINS_USED; i++)
        cpu_gpio_pin_init(i);
    for (int i = CPU_CTL_PIN_BASE; i < CPU_CTL_PIN_BASE + CPU_CTL_PINS_USED; i++)
        cpu_gpio_pin_init(i);

    if (!gpio_get(CPU_RESB_PIN))
        cpu_resb_timer = delayed_by_us(get_absolute_time(), cpu_get_reset_us());
}

void cpu_print_status(void)
{
    printf("CPU : %.2fMHz\n", cpu_get_phi2_hz() / MHZ);
}
