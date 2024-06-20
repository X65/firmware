/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mem.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/vreg.h"
#include "hw.h"
#include "main.h"
#include "mem.pio.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

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

#define IS_HW_ACCESS(address) ((address & 0xFFFFC0) == 0x00FFC0)

static int mem_read_chan;
static int mem_write_chan;

#define MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH 20
static uint32_t mem_cpu_address_bus_history[MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH];
static uint8_t mem_cpu_address_bus_history_index = 0;

// 0b 0 0 ... VAB RWB
#define CPU_VAB_MASK (1 << 24)
#define CPU_RWB_MASK (1 << 25)

// This interrupt handler runs on second CPU core
static void __attribute__((optimize("O1")))
mem_bus_pio_irq_handler(void)
{
    // clear RXUNDER flag
    MEM_BUS_PIO->fdebug = (1u << (PIO_FDEBUG_RXUNDER_LSB + MEM_BUS_SM));
    while (true)
    {
        // read address and flags
        // TODO: get rid of this >> 6 - update masks, push address << 2
        uint32_t address_bus = pio_sm_get(MEM_BUS_PIO, MEM_BUS_SM) >> 6;
        // exit if there was nothing to read
        if ((MEM_BUS_PIO->fdebug & (1u << (PIO_FDEBUG_RXUNDER_LSB + MEM_BUS_SM))) != 0)
        {
            // Clear the interrupt request
            pio_interrupt_clear(MEM_BUS_PIO, MEM_BUS_PIO_IRQ);
            return;
        }

        if (address_bus & CPU_VAB_MASK)
        {
            // address is "invalid", do nothing
        }
        else
        {
            if (main_active() && mem_cpu_address_bus_history_index < MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH)
            {
                mem_cpu_address_bus_history[mem_cpu_address_bus_history_index++] = address_bus;
            }

            if (IS_HW_ACCESS(address_bus))
            {
                if (address_bus & CPU_RWB_MASK)
                {
                    pio_sm_put_blocking(MEM_BUS_PIO, MEM_BUS_SM, hw_read(address_bus));
                }
                else
                {
                    hw_write(address_bus, pio_sm_get_blocking(MEM_BUS_PIO, MEM_BUS_SM));
                }
                return;
            }
            if (address_bus & CPU_RWB_MASK)
            { // CPU is reading
                // Trigger reading 1 byte from RAM to PIO tx FIFO
                dma_channel_set_read_addr(mem_read_chan, &ram[address_bus & 0xFFFF], true);
            }
            else
            { // CPU is writing
                // Trigger writing 1 byte PIO rx FIFO to RAM
                dma_channel_set_write_addr(mem_write_chan, &ram[address_bus & 0xFFFF], true);

                // NOTE: Memory Write is fire'n'forget. CPU BUS state machine does not wait
                // for Write completion, instead proceeds to next cycle, that will READ instruction
                // from memory. Above read `if` branch will be triggered by IRQ, to process and enqueue read
                // instruction in Memory FIFO, while Write is still in-progress.

                // The MEM_RAM_READ_SM is still disabled, so the Read will not start until Write finishes.
                // CPU BUS SM will stall waiting for Read data is TX FIFO, making a loooong PHI2 HIGH phase.
                // FIXME: Above describes setup with external memory - currently we have internal, that works "instantly"
            }
        }
    }
}

static void mem_bus_pio_init(void)
{
    // PIO to manage PHI2 clock and 65816 address/data bus
    uint offset = pio_add_program(MEM_BUS_PIO, &mem_bus_program);
    pio_sm_config config = mem_bus_program_get_default_config(offset);
    sm_config_set_clkdiv_int_frac(&config, MEM_BUS_PIO_CLKDIV_INT, 0); // FIXME: remove?
    sm_config_set_in_shift(&config, true, true, 26);
    sm_config_set_out_shift(&config, true, false, 0);
    sm_config_set_sideset_pins(&config, CPU_PHI2_PIN);
    sm_config_set_in_pins(&config, MEM_DATA_PIN_BASE);
    sm_config_set_out_pins(&config, MEM_DATA_PIN_BASE, 8);
    for (int i = MEM_BUS_PIN_BASE; i < MEM_BUS_PIN_BASE + MEM_BUS_PINS_USED; i++)
        pio_gpio_init(MEM_BUS_PIO, i);
    pio_sm_set_consecutive_pindirs(MEM_BUS_PIO, MEM_BUS_SM, MEM_DATA_PIN_BASE, 10, false);
    pio_sm_set_consecutive_pindirs(MEM_BUS_PIO, MEM_BUS_SM, CPU_PHI2_PIN, 4, true);
    gpio_pull_up(CPU_PHI2_PIN);
    pio_set_irq0_source_enabled(MEM_BUS_PIO, pis_interrupt0, true);
    pio_interrupt_clear(MEM_BUS_PIO, MEM_BUS_PIO_IRQ);
    pio_sm_init(MEM_BUS_PIO, MEM_BUS_SM, offset, &config);
    irq_set_exclusive_handler(MEM_BUS_IRQ, mem_bus_pio_irq_handler);
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

void mem_core1_init(void)
{
    // enable Bus IRQ handler on Core1
    irq_set_enabled(MEM_BUS_IRQ, true);
}

void mem_init(void)
{
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(10);
    set_sys_clock_khz(266000, true);
    main_reclock();

    // Adjustments for GPIO performance. Important!
    for (int i = MEM_BUS_PIN_BASE; i < MEM_BUS_PIN_BASE + MEM_BUS_PINS_USED; i++)
    {
        gpio_set_pulls(i, true, true);
        gpio_set_input_hysteresis_enabled(i, false);
        hw_set_bits(&MEM_BUS_PIO->input_sync_bypass, 1u << i);
    }

    // the inits
    mem_bus_pio_init();
    mem_dma_init();

    multicore_launch_core1(mem_core1_init);
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
    if (main_active())
    {
        if (mem_cpu_address_bus_history_index == MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH)
        {
            mem_cpu_address_bus_history_index++;
            for (int i = 0; i < MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH; i++)
            {
                printf("CPU: 0x%06lX %s\n",
                       mem_cpu_address_bus_history[i] & 0xFFFFFF,
                       mem_cpu_address_bus_history[i] & CPU_RWB_MASK ? "R" : "w");
            }
        }
    }
}

void mem_read_buf(uint32_t addr)
{
    if (IS_HW_ACCESS(addr))
    {
        for (size_t i = 0; i < mbuf_len; ++i)
        {
            mbuf[i] = hw_read(addr++);
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
    if (IS_HW_ACCESS(addr))
    {
        for (size_t i = 0; i < mbuf_len; ++i)
        {
            hw_write(addr++, mbuf[i]);
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
