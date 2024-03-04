/*
 * Copyright (c) 2023 Rumbledethumps
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mem.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "main.h"
#include "mem.pio.h"
#include <stdbool.h>
#include <stdio.h>

uint8_t mbuf[MBUF_SIZE] __attribute__((aligned(4)));
size_t mbuf_len;

static uint8_t mem_ram_current_bank;
static uint mem_ram_write_program_offset;
static uint mem_ram_read_program_offset;
static uint mem_ram_pio_set_pins_instruction;
static int mem_ram_write_dma_chan;
static dma_channel_config mem_ram_write_dma_config;

#define MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH 20
static uint32_t mem_cpu_address_bus_history[MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH];
static uint8_t mem_cpu_address_bus_history_index = 0;

static inline void pio_sm_clear_tx_fifo_stalled(PIO pio, uint sm)
{
    check_pio_param(pio);
    check_sm_param(sm);
    pio->fdebug = (1u << (PIO_FDEBUG_TXSTALL_LSB + sm));
}

static inline bool pio_sm_is_tx_fifo_stalled(PIO pio, uint sm)
{
    check_pio_param(pio);
    check_sm_param(sm);
    return (pio->fdebug & (1u << (PIO_FDEBUG_TXSTALL_LSB + sm))) != 0;
}

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

static void __attribute__((optimize("O1")))
mem_ram_write_dma_handler()
{
    if (pio_sm_is_tx_fifo_empty(MEM_RAM_PIO, MEM_RAM_WRITE_SM) && pio_sm_is_tx_fifo_stalled(MEM_RAM_PIO, MEM_RAM_WRITE_SM))
    {
        // clear the interrupt request
        dma_hw->ints0 = 1u << mem_ram_write_dma_chan;

        // stop Write SM
        pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_WRITE_SM, false);
        // end write Command by enabling some other bank
        pio_sm_exec(MEM_RAM_PIO, MEM_RAM_READ_SM, mem_ram_pio_set_pins_instruction | (~mem_ram_current_bank & 0b11));

        // re-enable Read SM
        pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_READ_SM, true);
    }
}

static void mem_ram_pio_init(void)
{
    // PIO to manage PSRAM memory read/writes
    mem_ram_write_program_offset = pio_add_program(MEM_RAM_PIO, &mem_spi_program);
    pio_sm_config config = mem_spi_program_get_default_config(mem_ram_write_program_offset);
    sm_config_set_clkdiv_int_frac(&config, 100, 0); // FIXME: remove?
    sm_config_set_in_shift(&config, false, true, 8);
    sm_config_set_out_shift(&config, false, true, 8);
    sm_config_set_sideset_pins(&config, MEM_RAM_CLK_PIN);
    sm_config_set_set_pins(&config, MEM_RAM_CE0_PIN, 2);
    sm_config_set_in_pins(&config, MEM_RAM_SIO0_PIN);
    sm_config_set_out_pins(&config, MEM_RAM_SIO0_PIN, 4);
    for (int i = MEM_RAM_PIN_BASE; i < MEM_RAM_PIN_BASE + MEM_RAM_PINS_USED; i++)
        pio_gpio_init(MEM_RAM_PIO, i);
    gpio_pull_down(MEM_RAM_CLK_PIN);
    pio_sm_set_consecutive_pindirs(MEM_RAM_PIO, MEM_RAM_WRITE_SM, MEM_RAM_PIN_BASE, MEM_RAM_PINS_USED, true);
    pio_sm_init(MEM_RAM_PIO, MEM_RAM_WRITE_SM, mem_ram_write_program_offset, &config);

    // create "set pins, .." instruction
    mem_ram_pio_set_pins_instruction = pio_encode_set(pio_pins, 0);

    // Switch all banks to QPI mode
    // Do it twice, just to make sure
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_WRITE_SM, true);
    for (int bank = 0; bank < MEM_RAM_BANKS * 2; bank++)
    {
        pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_WRITE_SM, mem_ram_pio_set_pins_instruction | (bank & 0b11));
        pio_sm_put(MEM_RAM_PIO, MEM_RAM_WRITE_SM, 0x35000000); // QPI Mode Enable 0x35, left aligned
        pio_sm_get_blocking(MEM_RAM_PIO, MEM_RAM_WRITE_SM);    // wait for completion
    }

    // Now load and run QPI programs

    // Write QPI
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_WRITE_SM, false);
    pio_remove_program(MEM_RAM_PIO, &mem_spi_program, mem_ram_write_program_offset);
    pio_sm_restart(MEM_RAM_PIO, MEM_RAM_WRITE_SM);

    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
    mem_ram_write_program_offset = pio_add_program(MEM_RAM_PIO, &mem_qpi_write_program);
    sm_config_set_wrap(&config, mem_ram_write_program_offset + mem_qpi_write_wrap_target, mem_ram_write_program_offset + mem_qpi_write_wrap);
    pio_sm_init(MEM_RAM_PIO, MEM_RAM_WRITE_SM, mem_ram_write_program_offset, &config);
    // keep disabled, to not interfere with Read SM

    // Read QPI
    sm_config_set_out_shift(&config, false, true, 32);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_NONE);
    pio_sm_set_consecutive_pindirs(MEM_RAM_PIO, MEM_RAM_READ_SM, MEM_RAM_PIN_BASE, MEM_RAM_PINS_USED, true);
    mem_ram_read_program_offset = pio_add_program(MEM_RAM_PIO, &mem_qpi_read_program);
    sm_config_set_wrap(&config, mem_ram_read_program_offset + mem_qpi_read_wrap_target, mem_ram_read_program_offset + mem_qpi_read_wrap);
    pio_sm_init(MEM_RAM_PIO, MEM_RAM_READ_SM, mem_ram_read_program_offset, &config);
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_READ_SM, true);

    // Write PIO DMA channel
    mem_ram_write_dma_chan = dma_claim_unused_channel(true);
    mem_ram_write_dma_config = dma_channel_get_default_config(mem_ram_write_dma_chan);
    channel_config_set_high_priority(&mem_ram_write_dma_config, true);
    channel_config_set_transfer_data_size(&mem_ram_write_dma_config, DMA_SIZE_8);
    channel_config_set_write_increment(&mem_ram_write_dma_config, false);
    channel_config_set_dreq(&mem_ram_write_dma_config, MEM_RAM_WRITE_DREQ);
    dma_channel_configure(mem_ram_write_dma_chan, &mem_ram_write_dma_config,
                          &MEM_RAM_PIO->txf[MEM_RAM_WRITE_SM],
                          NULL,
                          0,
                          false);
    dma_channel_set_irq0_enabled(mem_ram_write_dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, mem_ram_write_dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);

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

static void mem_read_sm_restart(uint sm)
{
    pio_sm_set_enabled(MEM_RAM_PIO, sm, false);
    pio_sm_clear_fifos(MEM_RAM_PIO, sm);
    pio_sm_restart(MEM_RAM_PIO, sm);
    pio_sm_clkdiv_restart(MEM_RAM_PIO, sm);
    pio_sm_exec(MEM_RAM_PIO, sm, pio_encode_jmp(mem_ram_read_program_offset));
    pio_sm_set_enabled(MEM_RAM_PIO, sm, true);
}

static void mem_read_id(int8_t bank, uint8_t ID[8])
{
    // enable memory bank
    // and push 2 nibbles (X, 0 indexed thus -1)
    pio_sm_put(MEM_RAM_PIO, MEM_RAM_READ_SM,
               (mem_ram_pio_set_pins_instruction | bank) << 16 //
                   | (2 - 1));

    // QPI Read ID 0x35, left aligned
    pio_sm_put(MEM_RAM_PIO, MEM_RAM_READ_SM, 0x9F000000);

    uint8_t i = 7;
    do
    {
        ID[i] = pio_sm_get_blocking(MEM_RAM_PIO, MEM_RAM_READ_SM);
    } while (i--);

    pio_sm_exec(MEM_RAM_PIO, MEM_RAM_READ_SM, mem_ram_pio_set_pins_instruction | (~bank & 0b11));
    mem_read_sm_restart(MEM_RAM_READ_SM);
}

void mem_read_buf(uint32_t addr)
{
    // enable memory bank
    // and push 8 nibbles (X, 0 indexed thus -1)
    mem_ram_current_bank = (addr & 0xFFFFFF) >> 22;
    pio_sm_put(MEM_RAM_PIO, MEM_RAM_READ_SM,
               (mem_ram_pio_set_pins_instruction | mem_ram_current_bank) << 16 //
                   | (8 - 1));

    uint32_t command = 0xEB000000 | (addr & 0xFFFFFF); // QPI Fast Read 0xEB
    pio_sm_put(MEM_RAM_PIO, MEM_RAM_READ_SM, command);

    for (uint16_t i = 0; i < mbuf_len; i++)
    {
        mbuf[i] = pio_sm_get_blocking(MEM_RAM_PIO, MEM_RAM_READ_SM);
    }

    pio_sm_exec(MEM_RAM_PIO, MEM_RAM_READ_SM, mem_ram_pio_set_pins_instruction | (~mem_ram_current_bank & 0b11));
    mem_read_sm_restart(MEM_RAM_READ_SM);
}

void mem_write_buf(uint32_t addr)
{
    // enable memory bank using Read SM, without blocking
    mem_ram_current_bank = (addr & 0xFFFFFF) >> 22;
    pio_sm_exec(MEM_RAM_PIO, MEM_RAM_READ_SM, mem_ram_pio_set_pins_instruction | (mem_ram_current_bank & 0b11));

    // and start writing ASAP
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_WRITE_SM, true);
    mbuf[0] = (uint8_t)0x38; // QPI Write 0x38
    mbuf[1] = (uint8_t)(addr >> 16);
    mbuf[2] = (uint8_t)(addr >> 8);
    mbuf[3] = (uint8_t)(addr);
    dma_channel_set_read_addr(mem_ram_write_dma_chan, mbuf, false);
    dma_channel_set_trans_count(mem_ram_write_dma_chan, mbuf_len, false);
    channel_config_set_read_increment(&mem_ram_write_dma_config, true);
    dma_channel_set_config(mem_ram_write_dma_chan, &mem_ram_write_dma_config, true);

    // disable Read SM so it does not interfere with write
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_READ_SM, false);

    pio_sm_clear_tx_fifo_stalled(MEM_RAM_PIO, MEM_RAM_WRITE_SM);
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
