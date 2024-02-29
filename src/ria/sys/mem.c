/*
 * Copyright (c) 2023 Rumbledethumps
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mem.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "main.h"
#include "mem.pio.h"
#include <stdio.h>

uint8_t mbuf[MBUF_SIZE] __attribute__((aligned(4)));
size_t mbuf_len;

static uint mem_ram_program_offset;

#define MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH 20
static uint32_t mem_cpu_address_bus_history[MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH];
static uint8_t mem_cpu_address_bus_history_index = 0;

static void __attribute__((optimize("O1")))
mem_bus_pio_irq_handler()
{
    // read address and flags
    uint32_t address_bus = pio_sm_get_blocking(MEM_BUS_PIO, MEM_BUS_SM) >> 6;
    uint32_t mem_cpu_address = address_bus & 0xffffff;
    uint8_t mem_cpu_lines = address_bus >> 24;
    (void)mem_cpu_lines;
    static uint32_t last_mem_cpu_address = 0x1234;
    if (mem_cpu_address_bus_history_index < MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH && last_mem_cpu_address != mem_cpu_address)
    {
        last_mem_cpu_address = mem_cpu_address;
        mem_cpu_address_bus_history[mem_cpu_address_bus_history_index++] = mem_cpu_address;
    }
    // printf("> %lx %x\n", mem_cpu_address, mem_cpu_lines);

    // feed NOP to CPU
    pio_sm_put_blocking(MEM_BUS_PIO, MEM_BUS_SM, 0xEA);

    // Clear the interrupt request.
    pio_interrupt_clear(MEM_BUS_PIO, MEM_BUS_PIO_IRQ);
}

static void mem_bus_pio_init(void)
{
    // PIO to manage PHI2 clock and 65816 address/data bus
    uint offset = pio_add_program(MEM_BUS_PIO, &mem_bus_program);
    pio_sm_config config = mem_bus_program_get_default_config(offset);
    sm_config_set_clkdiv_int_frac(&config, 10, 0); // FIXME: remove?
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
    irq_set_enabled(MEM_BUS_IRQ, true);
    pio_sm_set_enabled(MEM_BUS_PIO, MEM_BUS_SM, true);

    // // Need both channels now to configure chain ping-pong
    // int addr_chan = dma_claim_unused_channel(true);
    // int data_chan = dma_claim_unused_channel(true);

    // // DMA move the requested memory data to PIO for output
    // dma_channel_config data_dma = dma_channel_get_default_config(data_chan);
    // channel_config_set_high_priority(&data_dma, true);
    // channel_config_set_dreq(&data_dma, pio_get_dreq(MEM_BUS_PIO, MEM_BUS_SM, false));
    // channel_config_set_read_increment(&data_dma, false);
    // channel_config_set_transfer_data_size(&data_dma, DMA_SIZE_8);
    // channel_config_set_chain_to(&data_dma, addr_chan);
    // dma_channel_configure(
    //     data_chan,
    //     &data_dma,
    //     regs,                            // dst
    //     &MEM_BUS_PIO->rxf[MEM_BUS_SM], // src
    //     1,
    //     false);

    // // DMA move address from PIO into the data DMA config
    // dma_channel_config addr_dma = dma_channel_get_default_config(addr_chan);
    // channel_config_set_high_priority(&addr_dma, true);
    // channel_config_set_dreq(&addr_dma, pio_get_dreq(MEM_BUS_PIO, MEM_BUS_SM, false));
    // channel_config_set_read_increment(&addr_dma, false);
    // channel_config_set_chain_to(&addr_dma, data_chan);
    // dma_channel_configure(
    //     addr_chan,
    //     &addr_dma,
    //     &dma_channel_hw_addr(data_chan)->write_addr, // dst
    //     &MEM_BUS_PIO->rxf[MEM_BUS_SM],             // src
    //     1,
    //     true);
}

static void mem_ram_pio_init(void)
{
    // PIO to manage PSRAM memory read/writes
    mem_ram_program_offset = pio_add_program(MEM_RAM_PIO, &mem_spi_program);
    pio_sm_config config = mem_spi_program_get_default_config(mem_ram_program_offset);
    sm_config_set_clkdiv_int_frac(&config, 25, 0); // FIXME: remove?
    sm_config_set_in_shift(&config, false, true, 8);
    sm_config_set_out_shift(&config, false, true, 8);
    sm_config_set_sideset_pins(&config, MEM_RAM_CLK_PIN);
    sm_config_set_set_pins(&config, MEM_RAM_CE0_PIN, 2);
    sm_config_set_in_pins(&config, MEM_RAM_SIO0_PIN);
    sm_config_set_out_pins(&config, MEM_RAM_SIO0_PIN, 4);
    for (int i = MEM_RAM_PIN_BASE; i < MEM_RAM_PIN_BASE + MEM_RAM_PINS_USED; i++)
        pio_gpio_init(MEM_RAM_PIO, i);
    pio_sm_set_consecutive_pindirs(MEM_RAM_PIO, MEM_RAM_SM, MEM_RAM_PIN_BASE, MEM_RAM_PINS_USED, true);
    gpio_pull_up(MEM_RAM_CLK_PIN);
    pio_sm_init(MEM_RAM_PIO, MEM_RAM_SM, mem_ram_program_offset, &config);

    // Switch all banks to QPI mode
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_SM, true);
    for (int bank = 0; bank < MEM_RAM_BANKS; bank++)
    {
        pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_SM, pio_encode_set(pio_pins, bank));
        pio_sm_put(MEM_RAM_PIO, MEM_RAM_SM, 0x35000000); // QPI Mode Enable 0x35, left aligned
        pio_sm_get_blocking(MEM_RAM_PIO, MEM_RAM_SM);    // wait for completion
    }

    // Now load and run QPI program
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_SM, false);
    pio_remove_program(MEM_RAM_PIO, &mem_spi_program, mem_ram_program_offset);
    mem_ram_program_offset = pio_add_program(MEM_RAM_PIO, &mem_qpi_program);
    sm_config_set_wrap(&config, mem_ram_program_offset + mem_qpi_wrap_target, mem_ram_program_offset + mem_qpi_wrap);
    pio_sm_init(MEM_RAM_PIO, MEM_RAM_SM, mem_ram_program_offset, &config);
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_SM, true);

    // // Need both channels now to configure chain ping-pong
    // int addr_chan = dma_claim_unused_channel(true);
    // int data_chan = dma_claim_unused_channel(true);

    // // DMA move the requested memory data to PIO for output
    // dma_channel_config data_dma = dma_channel_get_default_config(data_chan);
    // channel_config_set_high_priority(&data_dma, true);
    // channel_config_set_dreq(&data_dma, pio_get_dreq(MEM_RAM_PIO, MEM_RAM_SM, true));
    // channel_config_set_transfer_data_size(&data_dma, DMA_SIZE_8);
    // channel_config_set_chain_to(&data_dma, addr_chan);
    // dma_channel_configure(
    //     data_chan,
    //     &data_dma,
    //     &MEM_RAM_PIO->txf[MEM_RAM_SM], // dst
    //     regs,                           // src
    //     1,
    //     false);

    // // DMA move address from PIO into the data DMA config
    // dma_channel_config addr_dma = dma_channel_get_default_config(addr_chan);
    // channel_config_set_high_priority(&addr_dma, true);
    // channel_config_set_dreq(&addr_dma, pio_get_dreq(MEM_RAM_PIO, MEM_RAM_SM, false));
    // channel_config_set_read_increment(&addr_dma, false);
    // channel_config_set_chain_to(&addr_dma, data_chan);
    // dma_channel_configure(
    //     addr_chan,
    //     &addr_dma,
    //     &dma_channel_hw_addr(data_chan)->read_addr, // dst
    //     &MEM_RAM_PIO->rxf[MEM_RAM_SM],             // src
    //     1,
    //     true);
}

void mem_init(void)
{
    // Adjustments for GPIO performance. Important!
    for (int i = MEM_BUS_PIN_BASE; i < MEM_BUS_PIN_BASE + MEM_BUS_PINS_USED; i++)
    {
        gpio_set_pulls(i, true, true);
        gpio_set_input_hysteresis_enabled(i, false);
        hw_set_bits(&MEM_BUS_PIO->input_sync_bypass, 1u << i);
    }
    for (int i = MEM_RAM_PIN_BASE; i < MEM_RAM_PIN_BASE + MEM_RAM_PINS_USED; i++)
    {
        gpio_set_pulls(i, true, true);
        gpio_set_input_hysteresis_enabled(i, false);
        hw_set_bits(&MEM_RAM_PIO->input_sync_bypass, 1u << i);
    }

    // the inits
    mem_bus_pio_init();
    mem_ram_pio_init();
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
                printf("CPU: 0x%3lX\n", mem_cpu_address_bus_history[i]);
            }
        }
    }
}

static void mem_read_id(int8_t bank, uint8_t ID[8])
{
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_SM, false);
    pio_sm_clear_fifos(MEM_RAM_PIO, MEM_RAM_SM);
    pio_sm_restart(MEM_RAM_PIO, MEM_RAM_SM);
    pio_sm_clkdiv_restart(MEM_RAM_PIO, MEM_RAM_SM);

    pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_SM, pio_encode_set(pio_pins, bank));
    pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_SM, pio_encode_set(pio_x, 6 - 2)); // 6 wait cycles
    pio_sm_exec(MEM_RAM_PIO, MEM_RAM_SM, pio_encode_jmp(mem_ram_program_offset));

    pio_sm_put(MEM_RAM_PIO, MEM_RAM_SM, 0x9F000000); // QPI Read ID 0x35, left aligned
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_SM, true);
    ID[7] = pio_sm_get_blocking(MEM_RAM_PIO, MEM_RAM_SM);
    ID[6] = pio_sm_get_blocking(MEM_RAM_PIO, MEM_RAM_SM);
    ID[5] = pio_sm_get_blocking(MEM_RAM_PIO, MEM_RAM_SM);
    ID[4] = pio_sm_get_blocking(MEM_RAM_PIO, MEM_RAM_SM);
    ID[3] = pio_sm_get_blocking(MEM_RAM_PIO, MEM_RAM_SM);
    ID[2] = pio_sm_get_blocking(MEM_RAM_PIO, MEM_RAM_SM);
    ID[1] = pio_sm_get_blocking(MEM_RAM_PIO, MEM_RAM_SM);
    ID[0] = pio_sm_get_blocking(MEM_RAM_PIO, MEM_RAM_SM);
    pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_SM, pio_encode_set(pio_pins, ~bank));
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_SM, false);
}

void mem_print_status(void)
{
    for (uint8_t i = 0; i < MEM_RAM_BANKS; i++)
    {
        uint8_t ID[8] = {0};
        mem_read_id(i, ID);
        // #ifndef NDEBUG
        //         printf("%llx\n", *((uint64_t *)ID));
        // #endif
        printf("MEM%d: ", i);
        switch (ID[7])
        {
        case 0x00:
        case 0xff:
            printf("Not Found\n");
            break;
        case 0x9d: // ISSI
        {
            uint8_t device_density = (ID[5] & 0b11100000) >> 5;
            printf("%2dMb ISSI (%llx)%s\n",
                   device_density == 0b000   ? 8
                   : device_density == 0b001 ? 16
                   : device_density == 0b010 ? 32
                                             : 0,
                   *((uint64_t *)ID) & 0xffffffffffff,
                   ID[6] != 0x5d ? " Not Passed" : 0);
        }
        break;
        default:
            printf("Unknown (0x%02x)\n", ID[7]);
        }
    }
}
