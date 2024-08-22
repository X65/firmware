/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "bus.h"
#include "bus.pio.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/vreg.h"
#include "main.h"
#include "pico/stdlib.h"
#include <stdbool.h>

#define MEM_BUS_PIO_CLKDIV_INT 10

static void mem_bus_pio_init(void)
{
    // PIO to manage PHI2 clock and 65816 address/data bus
    uint offset = pio_add_program(MEM_BUS_PIO, &mem_bus_program);
    pio_sm_config config = mem_bus_program_get_default_config(offset);
    sm_config_set_clkdiv_int_frac(&config, MEM_BUS_PIO_CLKDIV_INT, 0); // FIXME: remove?
    sm_config_set_in_shift(&config, true, true, 32);
    sm_config_set_out_shift(&config, true, false, 0);
    sm_config_set_sideset_pins(&config, BUS_CTL_PIN_BASE);
    sm_config_set_in_pins(&config, BUS_PIN_BASE);
    sm_config_set_out_pins(&config, BUS_DATA_PIN_BASE, 8);
    for (int i = BUS_PIN_BASE; i < BUS_PIN_BASE + BUS_DATA_PINS_USED; i++)
        pio_gpio_init(MEM_BUS_PIO, i);
    for (int i = BUS_CTL_PIN_BASE; i < BUS_CTL_PIN_BASE + BUS_CTL_PINS_USED; i++)
        pio_gpio_init(MEM_BUS_PIO, i);
    pio_sm_set_consecutive_pindirs(MEM_BUS_PIO, MEM_BUS_SM, BUS_PIN_BASE, BUS_DATA_PINS_USED, false);
    pio_sm_set_consecutive_pindirs(MEM_BUS_PIO, MEM_BUS_SM, BUS_CTL_PIN_BASE, BUS_CTL_PINS_USED, true);
    gpio_pull_up(CPU_PHI2_PIN);
    pio_sm_init(MEM_BUS_PIO, MEM_BUS_SM, offset, &config);
    pio_sm_set_enabled(MEM_BUS_PIO, MEM_BUS_SM, true);
}

void bus_init(void)
{
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(10);
    set_sys_clock_khz(266000, true);
    main_reclock();

    // Adjustments for GPIO performance. Important!
    for (int i = BUS_PIN_BASE; i < BUS_PIN_BASE + BUS_DATA_PINS_USED; ++i)
    {
        gpio_set_pulls(i, true, true);
        gpio_set_input_hysteresis_enabled(i, false);
        hw_set_bits(&MEM_BUS_PIO->input_sync_bypass, 1u << i);
    }
    for (int i = BUS_CTL_PIN_BASE; i < BUS_CTL_PIN_BASE + BUS_CTL_PINS_USED; ++i)
    {
        gpio_set_pulls(i, true, true);
        gpio_set_input_hysteresis_enabled(i, false);
        hw_set_bits(&MEM_BUS_PIO->input_sync_bypass, 1u << i);
    }

    // the inits
    mem_bus_pio_init();
}

void bus_run(void)
{
}

void bus_stop(void)
{
}

extern void dump_cpu_history(void);

void bus_task(void)
{
    // dump_cpu_history();
}
