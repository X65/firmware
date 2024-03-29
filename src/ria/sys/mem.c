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
#include "pico/multicore.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

uint8_t mbuf[MBUF_SIZE] __attribute__((aligned(4)));
size_t mbuf_len;

static uint mem_ram_pio_set_pins_instruction;
static uint mem_ram_ce_high_instruction;

static int mem_read_chan;
static int mem_write_chan;

static uint8_t mem_ram_ID[MEM_RAM_BANKS][8];

#define MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH 20
static uint32_t mem_cpu_address_bus_history[MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH];
static uint8_t mem_cpu_address_bus_history_index = 0;

static inline uint8_t get_chip_select(uint8_t bank)
{
    uint8_t ce = bank & 0b01;
    ce |= ~(ce << 1) & 0b10;
    return (ce & 0b11);
}

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
            if (address_bus & CPU_RWB_MASK)
            { // CPU is reading
                // enable memory bank
                pio_sm_put(MEM_RAM_PIO, MEM_RAM_READ_SM,
                           (mem_ram_pio_set_pins_instruction | get_chip_select((address_bus & 0xFFFFFF) >> 23)));
                // TODO: access bank address directly: address_bus[3] (after shifting << 2)

                uint32_t command = 0xEB000000 | (address_bus & 0xFFFFFF); // QPI Fast Read 0xEB
                pio_sm_put(MEM_RAM_PIO, MEM_RAM_READ_SM, command);

                // Trigger reading 1 byte
                pio_sm_put(MEM_RAM_PIO, MEM_RAM_READ_SM, (2 - 1));
                dma_channel_set_trans_count(mem_read_chan, 1, true);

                if (main_active() && mem_cpu_address_bus_history_index < MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH)
                {
                    mem_cpu_address_bus_history[mem_cpu_address_bus_history_index++] = address_bus & 0xFFFFFF;
                }
            }
            else
            { // CPU is writing
                // enable memory bank
                pio_sm_put(MEM_RAM_PIO, MEM_RAM_WRITE_SM,
                           (mem_ram_pio_set_pins_instruction | get_chip_select((address_bus & 0xFFFFFF) >> 23)) << 16
                               // Trigger writing 8 nybbles of command and 1 byte of data
                               | (8 + 2 - 1) << 4
                               // set pindirs to OUT
                               | 0b1111);

                // and start writing ASAP
                pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_WRITE_SM, true);

                // disable Read SM so it does not interfere with write
                pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_READ_SM, false);

                uint32_t command = 0x38000000 | (address_bus & 0xFFFFFF); // QPI Write 0x38
                pio_sm_put(MEM_RAM_PIO, MEM_RAM_WRITE_SM, command);

                // Fetch data from CPU Bus FIFO and push to RAM Write
                uint32_t data = pio_sm_get_blocking(MEM_BUS_PIO, MEM_BUS_SM) << 24;
                pio_sm_put(MEM_RAM_PIO, MEM_RAM_WRITE_SM, data);

                // NOTE: Memory Write is fire'n'forget. CPU BUS state machine does not wait
                // for Write completion, instead proceeds to next cycle, that will READ instruction
                // from memory. Above `if` branch will be triggered by IRQ, to process and enqueue read
                // instruction in Memory FIFO, while Write is still in-progress.
                // The MEM_RAM_READ_SM is still disabled, so the Read will not start until Write finishes.
                // CPU BUS SM will stall waiting for Read data is TX FIFO, making a loooong PHI2 HIGH phase.

                if (main_active() && mem_cpu_address_bus_history_index < MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH)
                {
                    mem_cpu_address_bus_history[mem_cpu_address_bus_history_index++] = (address_bus & 0xFFFFFF) | 0x80000000;
                }
            }
        }
    }
}

// This is called to signal end of memory write
static void __attribute__((optimize("O1")))
mem_ram_pio_irq_handler(void)
{
    // printf("mem_ram_pio_irq_handler\n");

    // stop Write SM
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_WRITE_SM, false);
    pio_sm_exec(MEM_RAM_PIO, MEM_RAM_WRITE_SM, mem_ram_ce_high_instruction);

    // re-enable Read SM
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_READ_SM, true);

    // Clear the interrupt request
    pio_interrupt_clear(MEM_RAM_PIO, MEM_RAM_PIO_IRQ);
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
    pio_sm_config write_config = mem_qpi_write_program_get_default_config(mem_ram_write_program_offset);
    sm_config_set_clkdiv_int_frac(&write_config, 100, 0); // FIXME: remove?
    sm_config_set_in_shift(&write_config, false, true, 8);
    sm_config_set_out_shift(&write_config, false, true, 32);
    sm_config_set_sideset_pins(&write_config, MEM_RAM_CLK_PIN);
    sm_config_set_set_pins(&write_config, MEM_RAM_CE0_PIN, 2);
    sm_config_set_in_pins(&write_config, MEM_RAM_SIO0_PIN);
    sm_config_set_out_pins(&write_config, MEM_RAM_SIO0_PIN, 4);

    // Copy write_config as template for other state machines
    pio_sm_config read_config = write_config;
    pio_sm_config spi_config = write_config;

    // Init Write SM
    sm_config_set_fifo_join(&write_config, PIO_FIFO_JOIN_TX); // we do not read using this PIO
    pio_sm_set_consecutive_pindirs(MEM_RAM_PIO, MEM_RAM_WRITE_SM, MEM_RAM_PIN_BASE, MEM_RAM_PINS_USED, true);
    pio_set_irq0_source_enabled(MEM_RAM_PIO, pis_interrupt0, true);
    pio_interrupt_clear(MEM_RAM_PIO, MEM_RAM_PIO_IRQ);
    pio_sm_init(MEM_RAM_PIO, MEM_RAM_WRITE_SM, mem_ram_write_program_offset, &write_config);
    irq_set_exclusive_handler(MEM_RAM_IRQ, mem_ram_pio_irq_handler);
    irq_set_enabled(MEM_RAM_IRQ, true);
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_WRITE_SM, false);

    // Read SPI program
    uint mem_ram_spi_program_offset = pio_add_program(MEM_RAM_PIO, &mem_spi_program);
    sm_config_set_wrap(&spi_config, mem_ram_spi_program_offset + mem_spi_wrap_target, mem_ram_spi_program_offset + mem_spi_wrap);
    sm_config_set_in_pins(&spi_config, MEM_RAM_SIO1_PIN);
    pio_sm_set_consecutive_pindirs(MEM_RAM_PIO, MEM_RAM_READ_SM, MEM_RAM_PIN_BASE, MEM_RAM_PINS_USED, true);
    pio_sm_init(MEM_RAM_PIO, MEM_RAM_READ_SM, mem_ram_spi_program_offset, &spi_config);
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_READ_SM, false);

    // Disable all memory banks (pull up both CE#) on all SMs waiting for their PIO to stabilize
    mem_ram_ce_high_instruction = mem_ram_pio_set_pins_instruction | 0b11;
    pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_WRITE_SM, mem_ram_ce_high_instruction);
    pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_READ_SM, mem_ram_ce_high_instruction);

    // memory chips require 150us to complete device initialization
    busy_wait_until(from_us_since_boot(150));

    // Reset all banks using QPI mode in case we are doing software reset and chips are in QPI mode
    for (int bank = 0; bank < MEM_RAM_BANKS; bank++)
    {
        pio_sm_put(MEM_RAM_PIO, MEM_RAM_WRITE_SM,
                   (mem_ram_pio_set_pins_instruction | get_chip_select(bank)) << 16
                       | (2 - 1) << 4
                       | 0b1111);
        pio_sm_put(MEM_RAM_PIO, MEM_RAM_WRITE_SM, 0x66000000); // RSTEN, left aligned
        pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_WRITE_SM, true);
        while ((MEM_RAM_PIO->ctrl & (1u << MEM_RAM_WRITE_SM)))
            tight_loop_contents(); // spin while SM enabled
        pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_READ_SM, false);
    }
    for (int bank = 0; bank < MEM_RAM_BANKS; bank++)
    {
        pio_sm_put(MEM_RAM_PIO, MEM_RAM_WRITE_SM,
                   (mem_ram_pio_set_pins_instruction | get_chip_select(bank)) << 16
                       | (2 - 1) << 4
                       | 0b1111);
        pio_sm_put(MEM_RAM_PIO, MEM_RAM_WRITE_SM, 0x99000000); // RST, left aligned
        pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_WRITE_SM, true);
        while ((MEM_RAM_PIO->ctrl & (1u << MEM_RAM_WRITE_SM)))
            tight_loop_contents(); // spin while SM enabled
        pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_READ_SM, false);
    }

    // Now reset using SPI mode in case we are after power up
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_READ_SM, true);
    for (int bank = 0; bank < MEM_RAM_BANKS; bank++)
    {
        pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_READ_SM, mem_ram_pio_set_pins_instruction | get_chip_select(bank));
        pio_sm_put(MEM_RAM_PIO, MEM_RAM_READ_SM, 0x66000000); // RSTEN, left aligned
        pio_sm_get_blocking(MEM_RAM_PIO, MEM_RAM_READ_SM);    // wait for completion
        mem_read_sm_restart(MEM_RAM_READ_SM, mem_ram_spi_program_offset);
    }
    for (int bank = 0; bank < MEM_RAM_BANKS; bank++)
    {
        pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_READ_SM, mem_ram_pio_set_pins_instruction | get_chip_select(bank));
        pio_sm_put(MEM_RAM_PIO, MEM_RAM_READ_SM, 0x99000000); // RST, left aligned
        pio_sm_get_blocking(MEM_RAM_PIO, MEM_RAM_READ_SM);    // wait for completion
        mem_read_sm_restart(MEM_RAM_READ_SM, mem_ram_spi_program_offset);
    }
    pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_READ_SM, mem_ram_ce_high_instruction);

    // Read memory IDs
    for (int bank = 0; bank < MEM_RAM_BANKS; bank++)
    {
        pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_READ_SM, mem_ram_pio_set_pins_instruction | get_chip_select(bank));

        pio_sm_put(MEM_RAM_PIO, MEM_RAM_READ_SM, 0x9F000000); // QPI Read ID, left aligned
        uint8_t i = 7;
        do
        {
            mem_ram_ID[bank][i] = pio_sm_get_blocking(MEM_RAM_PIO, MEM_RAM_READ_SM);
        } while (i--);

        mem_read_sm_restart(MEM_RAM_READ_SM, mem_ram_spi_program_offset);
    }
    pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_READ_SM, mem_ram_ce_high_instruction);

    // Toggle Wrap boundary to 32 bytes
    for (int bank = 0; bank < MEM_RAM_BANKS; bank++)
    {
        pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_READ_SM, mem_ram_pio_set_pins_instruction | get_chip_select(bank));
        pio_sm_put(MEM_RAM_PIO, MEM_RAM_READ_SM, 0xC0000000); // Wrap Boundary Toggle, left aligned
        pio_sm_get_blocking(MEM_RAM_PIO, MEM_RAM_READ_SM);    // wait for completion
        mem_read_sm_restart(MEM_RAM_READ_SM, mem_ram_spi_program_offset);
    }
    pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_READ_SM, mem_ram_ce_high_instruction);

    // Finally enable QPI mode
    for (int bank = 0; bank < MEM_RAM_BANKS; bank++)
    {
        pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_READ_SM, mem_ram_pio_set_pins_instruction | get_chip_select(bank));
        pio_sm_put(MEM_RAM_PIO, MEM_RAM_READ_SM, 0x35000000); // QPI Mode Enable 0x35, left aligned
        pio_sm_get_blocking(MEM_RAM_PIO, MEM_RAM_READ_SM);    // wait for completion
        mem_read_sm_restart(MEM_RAM_READ_SM, mem_ram_spi_program_offset);
    }
    pio_sm_exec_wait_blocking(MEM_RAM_PIO, MEM_RAM_READ_SM, mem_ram_ce_high_instruction);
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_READ_SM, false);

    // Do not need SPI program anymore
    pio_remove_program(MEM_RAM_PIO, &mem_spi_program, mem_ram_spi_program_offset);

    // Read QPI program
    uint mem_ram_read_program_offset = pio_add_program(MEM_RAM_PIO, &mem_qpi_read_program);
    sm_config_set_wrap(&read_config, mem_ram_read_program_offset + mem_qpi_read_wrap_target, mem_ram_read_program_offset + mem_qpi_read_wrap);
    pio_sm_set_consecutive_pindirs(MEM_RAM_PIO, MEM_RAM_READ_SM, MEM_RAM_PIN_BASE, MEM_RAM_PINS_USED, true);
    pio_sm_init(MEM_RAM_PIO, MEM_RAM_READ_SM, mem_ram_read_program_offset, &read_config);
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

void mem_core1_init(void)
{
    // enable Bus IRQ handler on Core1
    irq_set_enabled(MEM_BUS_IRQ, true);
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
                       mem_cpu_address_bus_history[i] & 0x80000000 ? "w" : "R");
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
    assert(mbuf_len <= 32); // Memory Write wraps at 32 bytes

    // enable memory bank
    pio_sm_put(MEM_RAM_PIO, MEM_RAM_WRITE_SM,
               (mem_ram_pio_set_pins_instruction | get_chip_select((addr & 0xFFFFFF) >> 23)) << 16
                   // Write 8 nybbles (half-bytes) of command (2 command, 6 address)
                   // and mbuf_len*2 nybbles, 0-offset thus -1
                   | (8 + mbuf_len * 2 - 1) << 4
                   // set pindirs to OUT
                   | 0b1111);

    // and start writing ASAP
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_WRITE_SM, true);

    // disable Read SM so it does not interfere with write
    pio_sm_set_enabled(MEM_RAM_PIO, MEM_RAM_READ_SM, false);

    uint32_t command = 0x38000000 | (addr & 0xFFFFFF); // QPI Write 0x38
    pio_sm_put(MEM_RAM_PIO, MEM_RAM_WRITE_SM, command);

    for (size_t i = 0; i < mbuf_len; i += 4)
    {
        uint32_t data = mbuf[i] << 24 | mbuf[i + 1] << 16 | mbuf[i + 2] << 8 | mbuf[i + 3];
        pio_sm_put_blocking(MEM_RAM_PIO, MEM_RAM_WRITE_SM, data);
    }
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
