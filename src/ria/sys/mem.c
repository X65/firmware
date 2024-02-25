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

static void __attribute__((optimize("O1")))
mem_bus_pio_irq_handler()
{
    // read address and flags
    uint32_t address = pio_sm_get_blocking(MEM_BUS_PIO, MEM_BUS_SM) >> 6;
    // printf("> %lx\n", address);

    // feed NOP to CPU
    pio_sm_put_blocking(MEM_BUS_PIO, MEM_BUS_SM, 0xEA);

    // Clear the interrupt request.
    pio_interrupt_clear(MEM_BUS_PIO, MEM_BUS_PIO_IRQ);
}

static void mem_bus_pio_init(void)
{
    // PIO to manage PHI2 clock and 6502 writes
    uint offset = pio_add_program(MEM_BUS_PIO, &mem_bus_program);
    pio_sm_config config = mem_bus_program_get_default_config(offset);
    sm_config_set_clkdiv_int_frac(&config, 10, 0); // FIXME: remove?
    sm_config_set_sideset_pins(&config, CPU_PHI2_PIN);
    sm_config_set_in_pins(&config, MEM_DATA_PIN_BASE);
    sm_config_set_in_shift(&config, true, true, 26);
    sm_config_set_out_pins(&config, MEM_DATA_PIN_BASE, 8);
    sm_config_set_out_shift(&config, true, false, 0);
    for (int i = MEM_DATA_PIN_BASE; i < MEM_BUS_PIN_BASE + MEM_BUS_PINS_USED; i++)
        pio_gpio_init(MEM_BUS_PIO, i);
    pio_sm_set_consecutive_pindirs(MEM_BUS_PIO, MEM_BUS_SM, MEM_DATA_PIN_BASE, 10, false);
    pio_sm_set_consecutive_pindirs(MEM_BUS_PIO, MEM_BUS_SM, CPU_PHI2_PIN, 4, true);
    gpio_pull_up(CPU_PHI2_PIN);
    pio_set_irq0_source_enabled(MEM_BUS_PIO, pis_interrupt0, true);
    pio_interrupt_clear(MEM_BUS_PIO, MEM_BUS_PIO_IRQ);
    pio_sm_init(MEM_BUS_PIO, MEM_BUS_SM, offset, &config);
    irq_set_exclusive_handler(MEM_BUS_IRQ, mem_bus_pio_irq_handler);
    irq_set_enabled(MEM_BUS_IRQ, true);
    // pio_sm_put(MEM_BUS_PIO, MEM_BUS_SM, (uintptr_t)regs >> 5);
    // pio_sm_exec_wait_blocking(MEM_BUS_PIO, MEM_BUS_SM, pio_encode_pull(false, true));
    // pio_sm_exec_wait_blocking(MEM_BUS_PIO, MEM_BUS_SM, pio_encode_mov(pio_y, pio_osr));
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

static void mem_qpi_pio_init(void)
{
    // PIO for 6502 reads
    uint offset = pio_add_program(MEM_QPI_PIO, &mem_qpi_program);
    pio_sm_config config = mem_qpi_program_get_default_config(offset);
    sm_config_set_in_pins(&config, MEM_QPI_PIN_BASE);
    sm_config_set_in_shift(&config, false, true, 5);
    // sm_config_set_out_pins(&config, RIA_DATA_PIN_BASE, 8);
    // sm_config_set_out_shift(&config, true, true, 8);
    // for (int i = RIA_DATA_PIN_BASE; i < RIA_DATA_PIN_BASE + 8; i++)
    //     pio_gpio_init(MEM_QPI_PIO, i);
    // pio_sm_set_consecutive_pindirs(MEM_QPI_PIO, MEM_QPI_SM, RIA_DATA_PIN_BASE, 8, true);
    // pio_sm_init(MEM_QPI_PIO, MEM_QPI_SM, offset, &config);
    // pio_sm_put(MEM_QPI_PIO, MEM_QPI_SM, (uintptr_t)regs >> 5);
    // pio_sm_exec_wait_blocking(MEM_QPI_PIO, MEM_QPI_SM, pio_encode_pull(false, true));
    // pio_sm_exec_wait_blocking(MEM_QPI_PIO, MEM_QPI_SM, pio_encode_mov(pio_y, pio_osr));
    // pio_sm_set_enabled(MEM_QPI_PIO, MEM_QPI_SM, true);

    // // Need both channels now to configure chain ping-pong
    // int addr_chan = dma_claim_unused_channel(true);
    // int data_chan = dma_claim_unused_channel(true);

    // // DMA move the requested memory data to PIO for output
    // dma_channel_config data_dma = dma_channel_get_default_config(data_chan);
    // channel_config_set_high_priority(&data_dma, true);
    // channel_config_set_dreq(&data_dma, pio_get_dreq(MEM_QPI_PIO, MEM_QPI_SM, true));
    // channel_config_set_transfer_data_size(&data_dma, DMA_SIZE_8);
    // channel_config_set_chain_to(&data_dma, addr_chan);
    // dma_channel_configure(
    //     data_chan,
    //     &data_dma,
    //     &MEM_QPI_PIO->txf[MEM_QPI_SM], // dst
    //     regs,                           // src
    //     1,
    //     false);

    // // DMA move address from PIO into the data DMA config
    // dma_channel_config addr_dma = dma_channel_get_default_config(addr_chan);
    // channel_config_set_high_priority(&addr_dma, true);
    // channel_config_set_dreq(&addr_dma, pio_get_dreq(MEM_QPI_PIO, MEM_QPI_SM, false));
    // channel_config_set_read_increment(&addr_dma, false);
    // channel_config_set_chain_to(&addr_dma, data_chan);
    // dma_channel_configure(
    //     addr_chan,
    //     &addr_dma,
    //     &dma_channel_hw_addr(data_chan)->read_addr, // dst
    //     &MEM_QPI_PIO->rxf[MEM_QPI_SM],             // src
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
        hw_set_bits(&pio0->input_sync_bypass, 1u << i);
        hw_set_bits(&pio1->input_sync_bypass, 1u << i);
    }
    for (int i = MEM_QPI_PIN_BASE; i < MEM_QPI_PIN_BASE + MEM_QPI_PINS_USED; i++)
    {
        gpio_set_pulls(i, true, true);
        gpio_set_input_hysteresis_enabled(i, false);
        hw_set_bits(&pio0->input_sync_bypass, 1u << i);
        hw_set_bits(&pio1->input_sync_bypass, 1u << i);
    }

    // the inits
    mem_bus_pio_init();
    mem_qpi_pio_init();
}

void mem_task(void)
{
}

static const uint8_t READ_ID = 0x9f;

static void mem_read_id(int8_t bank, uint8_t ID[8])
{
    const uint8_t COMMAND[4] = {
        READ_ID
        // next 24 bit address, unused by command
    };

    // TODO: implement
    (void)COMMAND;
    (void)bank;
    (void)ID;
}

void mem_print_status(void)
{
    for (uint8_t i = 0; i < MEM_BANKS; i++)
    {
        uint8_t ID[8] = {0};
        mem_read_id(i, ID);
        // #ifndef NDEBUG
        //         printf("%llx\n", *((uint64_t *)ID));
        // #endif
        printf("MEM%d: ", i);
        switch (ID[0])
        {
        case 0:
            printf("Not Found\n");
            break;
        case 0x9d: // ISSI
        {
            uint8_t device_density = (ID[2] & 0b11100000) >> 5;
            printf("%dMb ISSI%s\n",
                   device_density == 0b000   ? 8
                   : device_density == 0b001 ? 16
                   : device_density == 0b010 ? 32
                                             : 0,
                   ID[1] != 0x5d ? " Not Passed" : 0);
        }
        break;
        default:
            printf("Unknown (0x%02x)\n", ID[0]);
        }
    }
}
