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
#include "hardware/timer.h"
#include "main.h"
#include "mem.pio.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

uint8_t mbuf[MBUF_SIZE] __attribute__((aligned(4)));
size_t mbuf_len;

static uint mem_ram_read_program_offset;
static uint mem_ram_pio_set_pins_instruction;
static uint mem_ram_ce_high_instruction;

static int mem_read_chan;
static int mem_write_chan;

static uint8_t mem_ram_ID[MEM_RAM_BANKS][8];

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

static inline uint8_t get_chip_select(uint8_t bank)
{
    uint8_t ce = bank & 0b01;
    ce |= ~(ce << 1) & 0b10;
    return (ce & 0b11);
}

// 0b 0 0 ... VAB RWB
#define CPU_RWB_MASK (1 << 24)
#define CPU_VAB_MASK (1 << 25)

static void __attribute__((optimize("O1")))
mem_bus_pio_irq_handler(void)
{
    // printf("mem_bus_pio_irq_handler\n");
    // read address and flags
    uint32_t address_bus = pio_sm_get(MEM_BUS_PIO, MEM_BUS_SM) >> 6;

    if (address_bus & CPU_VAB_MASK)
    {
        // address is "invalid", so we can return anything
        // push WAI opcode to halt if CPU would process it as instruction
        pio_sm_put(MEM_BUS_PIO, MEM_BUS_SM, 0xCB);
        // TODO: move to PIO code?
    }
    else
    {
        if (address_bus & CPU_RWB_MASK)
        { // CPU is reading
            // enable memory bank
            pio_sm_put(MEM_RAM_PIO, MEM_RAM_READ_SM,
                       (mem_ram_pio_set_pins_instruction | get_chip_select((address_bus & 0xFFFFFF) >> 23)));

            uint32_t command = 0xEB000000 | (address_bus & 0xFFFFFF); // QPI Fast Read 0xEB
            pio_sm_put(MEM_RAM_PIO, MEM_RAM_READ_SM, command);

            // Trigger reading 1 byte
            pio_sm_put(MEM_RAM_PIO, MEM_RAM_READ_SM, (2 - 1));
            dma_channel_set_trans_count(mem_read_chan, 1, true);

            if (main_active() && mem_cpu_address_bus_history_index < MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH)
            {
                mem_cpu_address_bus_history[mem_cpu_address_bus_history_index++] = address_bus & 0xFFFFFF;
            }
            // printf("> %lx %x\n", mem_cpu_address, mem_cpu_lines);
        }
        else
        { // CPU is writing
            printf("CPU is writing somehow\?\?!\n");
        }
    }

    // Clear the interrupt request
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
}

static void mem_read_sm_restart(uint sm, uint addr)
{
    pio_sm_set_enabled(MEM_RAM_PIO, sm, false);
    pio_sm_clear_fifos(MEM_RAM_PIO, sm);
    pio_sm_restart(MEM_RAM_PIO, sm);
    pio_sm_clkdiv_restart(MEM_RAM_PIO, sm);
    pio_sm_exec(MEM_RAM_PIO, sm, pio_encode_jmp(addr));
    pio_sm_set_enabled(MEM_RAM_PIO, sm, true);
}

static void mem_ram_wait_for_write_pio(void)
{
    pio_sm_clear_tx_fifo_stalled(MEM_RAM_PIO, MEM_RAM_WRITE_SM);
    do
        tight_loop_contents();
    while (!pio_sm_is_tx_fifo_empty(MEM_RAM_PIO, MEM_RAM_WRITE_SM) || !pio_sm_is_tx_fifo_stalled(MEM_RAM_PIO, MEM_RAM_WRITE_SM));
}

static void mem_ram_pio_init(void)
{
    // PIO to manage PSRAM memory read/writes

    // Grab GPIO pins
    for (int i = MEM_RAM_PIN_BASE; i < MEM_RAM_PIN_BASE + MEM_RAM_PINS_USED; i++)
        pio_gpio_init(MEM_RAM_PIO, i);
    gpio_pull_down(MEM_RAM_CLK_PIN);

    // create "set pins, .." instruction
    mem_ram_pio_set_pins_instruction = pio_encode_set(pio_pins, 0);

    // Write QPI program
    uint mem_ram_write_program_offset = pio_add_program(MEM_RAM_PIO, &mem_qpi_write_program);
    pio_sm_config config = mem_qpi_write_program_get_default_config(mem_ram_write_program_offset);
    sm_config_set_clkdiv_int_frac(&config, 100, 0); // FIXME: remove?
    sm_config_set_in_shift(&config, false, true, 8);
    sm_config_set_out_shift(&config, false, true, 8);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX); // we do not read using this PIO
    sm_config_set_sideset_pins(&config, MEM_RAM_CLK_PIN);
    sm_config_set_set_pins(&config, MEM_RAM_CE0_PIN, 2);
    sm_config_set_in_pins(&config, MEM_RAM_SIO0_PIN);
    sm_config_set_out_pins(&config, MEM_RAM_SIO0_PIN, 4);
    pio_sm_set_consecutive_pindirs(MEM_RAM_PIO, MEM_RAM_WRITE_SM, MEM_RAM_PIN_BASE, MEM_RAM_PINS_USED, true);
    pio_sm_init(MEM_RAM_PIO, MEM_RAM_WRITE_SM, mem_ram_write_program_offset, &config);
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_WRITE_SM, false);

    // Read QPI
    sm_config_set_out_shift(&config, false, true, 32);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_NONE);
    mem_ram_read_program_offset = pio_add_program(MEM_RAM_PIO, &mem_qpi_read_program);
    sm_config_set_wrap(&config, mem_ram_read_program_offset + mem_qpi_read_wrap_target, mem_ram_read_program_offset + mem_qpi_read_wrap);
    pio_sm_set_consecutive_pindirs(MEM_RAM_PIO, MEM_RAM_READ_SM, MEM_RAM_PIN_BASE, MEM_RAM_PINS_USED, true);
    pio_sm_init(MEM_RAM_PIO, MEM_RAM_READ_SM, mem_ram_read_program_offset, &config);
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_READ_SM, false);

    // Program for SPI mode
    uint mem_ram_spi_program_offset = pio_add_program(MEM_RAM_PIO, &mem_spi_program);
    sm_config_set_wrap(&config, mem_ram_spi_program_offset + mem_spi_wrap_target, mem_ram_spi_program_offset + mem_spi_wrap);
    sm_config_set_in_pins(&config, MEM_RAM_SIO1_PIN);
    pio_sm_set_consecutive_pindirs(MEM_RAM_PIO, MEM_RAM_SPI_SM, MEM_RAM_PIN_BASE, MEM_RAM_PINS_USED, true);
    pio_sm_init(MEM_RAM_PIO, MEM_RAM_SPI_SM, mem_ram_spi_program_offset, &config);
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_SPI_SM, false);

    // Disable all memory banks (pull up both CE#) on all SMs waiting for their PIO to stabilize
    mem_ram_ce_high_instruction = mem_ram_pio_set_pins_instruction | 0b11;
    pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_WRITE_SM, mem_ram_ce_high_instruction);
    pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_READ_SM, mem_ram_ce_high_instruction);
    pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_SPI_SM, mem_ram_ce_high_instruction);

    // memory chips require 150us to complete device initialization
    busy_wait_until(from_us_since_boot(150));

    // Reset all banks using QPI mode in case we are doing software reset and chips are in QPI mode
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_WRITE_SM, true);
    for (int bank = 0; bank < MEM_RAM_BANKS; bank++)
    {
        pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_WRITE_SM, mem_ram_pio_set_pins_instruction | get_chip_select(bank));
        pio_sm_put(MEM_RAM_PIO, MEM_RAM_WRITE_SM, 0x66000000); // RSTEN, left aligned
        mem_ram_wait_for_write_pio();
    }
    for (int bank = 0; bank < MEM_RAM_BANKS; bank++)
    {
        pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_WRITE_SM, mem_ram_pio_set_pins_instruction | get_chip_select(bank));
        pio_sm_put(MEM_RAM_PIO, MEM_RAM_WRITE_SM, 0x99000000); // RST, left aligned
        mem_ram_wait_for_write_pio();
    }
    pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_WRITE_SM, mem_ram_ce_high_instruction);
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_WRITE_SM, false);

    // Now reset using SPI mode in case we are after power up
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_SPI_SM, true);
    for (int bank = 0; bank < MEM_RAM_BANKS; bank++)
    {
        pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_SPI_SM, mem_ram_pio_set_pins_instruction | get_chip_select(bank));
        pio_sm_put(MEM_RAM_PIO, MEM_RAM_SPI_SM, 0x66000000); // RSTEN, left aligned
        pio_sm_get_blocking(MEM_RAM_PIO, MEM_RAM_SPI_SM);    // wait for completion
        mem_read_sm_restart(MEM_RAM_SPI_SM, mem_ram_spi_program_offset);
    }
    for (int bank = 0; bank < MEM_RAM_BANKS; bank++)
    {
        pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_SPI_SM, mem_ram_pio_set_pins_instruction | get_chip_select(bank));
        pio_sm_put(MEM_RAM_PIO, MEM_RAM_SPI_SM, 0x99000000); // RST, left aligned
        pio_sm_get_blocking(MEM_RAM_PIO, MEM_RAM_SPI_SM);    // wait for completion
        mem_read_sm_restart(MEM_RAM_SPI_SM, mem_ram_spi_program_offset);
    }
    pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_SPI_SM, mem_ram_ce_high_instruction);

    // Read memory IDs
    for (int bank = 0; bank < MEM_RAM_BANKS; bank++)
    {
        pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_SPI_SM, mem_ram_pio_set_pins_instruction | get_chip_select(bank));

        pio_sm_put(MEM_RAM_PIO, MEM_RAM_SPI_SM, 0x9F000000); // QPI Read ID, left aligned
        uint8_t i = 7;
        do
        {
            mem_ram_ID[bank][i] = pio_sm_get_blocking(MEM_RAM_PIO, MEM_RAM_SPI_SM);
        } while (i--);

        mem_read_sm_restart(MEM_RAM_SPI_SM, mem_ram_spi_program_offset);
    }
    pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_SPI_SM, mem_ram_ce_high_instruction);

    // Toggle Wrap boundary to 32 bytes
    for (int bank = 0; bank < MEM_RAM_BANKS; bank++)
    {
        pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_SPI_SM, mem_ram_pio_set_pins_instruction | get_chip_select(bank));
        pio_sm_put(MEM_RAM_PIO, MEM_RAM_SPI_SM, 0xC0000000); // Wrap Boundary Toggle, left aligned
        pio_sm_get_blocking(MEM_RAM_PIO, MEM_RAM_SPI_SM);    // wait for completion
        mem_read_sm_restart(MEM_RAM_SPI_SM, mem_ram_spi_program_offset);
    }
    pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_SPI_SM, mem_ram_ce_high_instruction);

    // Finally enable QPI mode
    for (int bank = 0; bank < MEM_RAM_BANKS; bank++)
    {
        pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_SPI_SM, mem_ram_pio_set_pins_instruction | get_chip_select(bank));
        pio_sm_put(MEM_RAM_PIO, MEM_RAM_SPI_SM, 0x35000000); // QPI Mode Enable 0x35, left aligned
        pio_sm_get_blocking(MEM_RAM_PIO, MEM_RAM_SPI_SM);    // wait for completion
        mem_read_sm_restart(MEM_RAM_SPI_SM, mem_ram_spi_program_offset);
    }
    pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_SPI_SM, mem_ram_ce_high_instruction);
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_SPI_SM, false);

    // Do not need SPI program anymore
    pio_remove_program(MEM_RAM_PIO, &mem_spi_program, mem_ram_spi_program_offset);

    // Reads are most often and require fast reaction, so enable Read SM by default
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_READ_SM, true);
}

static void mem_dma_init(void)
{
    mem_read_chan = dma_claim_unused_channel(true);
    dma_channel_config read_dma = dma_channel_get_default_config(mem_read_chan);
    channel_config_set_high_priority(&read_dma, true);
    channel_config_set_dreq(&read_dma, pio_get_dreq(MEM_RAM_PIO, MEM_RAM_READ_SM, false));
    channel_config_set_read_increment(&read_dma, false);
    channel_config_set_transfer_data_size(&read_dma, DMA_SIZE_8);
    dma_channel_configure(
        mem_read_chan,
        &read_dma,
        &MEM_BUS_PIO->txf[MEM_BUS_SM],      // dst
        &MEM_RAM_PIO->rxf[MEM_RAM_READ_SM], // src
        1,
        false);

    mem_write_chan = dma_claim_unused_channel(true);
    dma_channel_config write_dma = dma_channel_get_default_config(mem_write_chan);
    channel_config_set_high_priority(&write_dma, true);
    channel_config_set_dreq(&write_dma, pio_get_dreq(MEM_BUS_PIO, MEM_BUS_SM, false));
    channel_config_set_read_increment(&write_dma, false);
    channel_config_set_transfer_data_size(&write_dma, DMA_SIZE_8);
    dma_channel_configure(
        mem_write_chan,
        &write_dma,
        &MEM_RAM_PIO->txf[MEM_RAM_WRITE_SM], // dst
        &MEM_BUS_PIO->rxf[MEM_BUS_SM],       // src
        1,
        false);
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
    mem_dma_init();
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
                printf("CPU: 0x%06lX\n", mem_cpu_address_bus_history[i]);
            }
        }
    }
}

void mem_read_buf(uint32_t addr)
{
    // enable memory bank
    pio_sm_put(MEM_RAM_PIO, MEM_RAM_READ_SM,
               (mem_ram_pio_set_pins_instruction | get_chip_select((addr & 0xFFFFFF) >> 23)));

    uint32_t command = 0xEB000000 | (addr & 0xFFFFFF); // QPI Fast Read 0xEB
    pio_sm_put(MEM_RAM_PIO, MEM_RAM_READ_SM, command);

    // Read mbuf_len*2 nybbles (half-bytes), 0-offset thus -1
    pio_sm_put(MEM_RAM_PIO, MEM_RAM_READ_SM, mbuf_len * 2 - 1);

    for (uint16_t i = 0; i < mbuf_len; i++)
    {
        mbuf[i] = pio_sm_get_blocking(MEM_RAM_PIO, MEM_RAM_READ_SM);
    }
}

void mem_write_buf(uint32_t addr)
{
    // enable memory bank using Read SM, without blocking
    pio_sm_exec(MEM_RAM_PIO, MEM_RAM_READ_SM,
                mem_ram_pio_set_pins_instruction | get_chip_select((addr & 0xFFFFFF) >> 23));

    // and start writing ASAP
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_WRITE_SM, true);

    pio_sm_put(MEM_RAM_PIO, MEM_RAM_WRITE_SM, 0x38000000); // QPI Write 0x38

    // disable Read SM so it does not interfere with write
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_READ_SM, false);

    pio_sm_put(MEM_RAM_PIO, MEM_RAM_WRITE_SM, addr << 8);
    pio_sm_put(MEM_RAM_PIO, MEM_RAM_WRITE_SM, addr << 16);
    pio_sm_put(MEM_RAM_PIO, MEM_RAM_WRITE_SM, addr << 24);

    for (uint16_t i = 0; i < mbuf_len; i++)
    {
        pio_sm_put_blocking(MEM_RAM_PIO, MEM_RAM_WRITE_SM, mbuf[i] << 24);
    }

    mem_ram_wait_for_write_pio();

    // stop Write SM
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_WRITE_SM, false);
    // finish Write Command by disabling chip select
    pio_sm_exec(MEM_RAM_PIO, MEM_RAM_READ_SM, mem_ram_ce_high_instruction);
    // re-enable Read SM
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_READ_SM, true);
}

void mem_print_status(void)
{
    for (uint8_t bank = 0; bank < MEM_RAM_BANKS; bank++)
    {
        uint8_t *ID = mem_ram_ID[bank];
        // #ifndef NDEBUG
        // printf("%llx\n", *((uint64_t *)ID));
        // #endif
        printf("MEM%d: ", bank);
        switch (ID[7])
        {
        case 0x00:
        case 0xff:
            printf("Not Found\n");
            break;
        case 0x0d: // ESP
        case 0x9d: // ISSI
        {
            uint8_t device_density = (ID[5] & 0b11100000) >> 5;
            uint8_t size = 0;
            switch (device_density)
            {
            case 0b000:
                size = 8;
                break;
            case 0b001:
                size = 16;
                break;
            case 0b010:
                size = 32;
                break;
            }
            if (ID[7] == 0x0d) // ESP reports differently than ISSI
                size *= 2;
            printf("%2dMb %s (%llx)%s\n",
                   size,
                   ID[7] == 0x0d ? "ESP" : "ISSI",
                   *((uint64_t *)ID) & 0xffffffffffff,
                   ID[6] != 0x5d ? " Not Passed" : 0);
        }
        break;
        default:
            printf("Unknown (0x%02x)\n", ID[7]);
        }
    }
}
