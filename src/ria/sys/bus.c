/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "bus.h"
#include "bus.pio.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/structs/bus_ctrl.h"
#include "main.h"
#include "pico/rand.h"
#include "pico/time.h"
#include "sys/com.h"
#include "sys/cpu.h"
#include "sys/mem.h"
#include <stdbool.h>

#define MEM_BUS_PIO_CLKDIV_INT 10

static volatile bool irq_enabled = false;

// #define MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH 50
// // #define MEM_CPU_ADDRESS_BUS_DUMP
// #define ABORT_ON_IRQ_BRK_READ              2
#ifdef MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH
static uint32_t mem_cpu_address_bus_history[MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH];
static uint8_t mem_cpu_address_bus_history_index = 0;
#ifdef ABORT_ON_IRQ_BRK_READ
#include <stdio.h>
static uint8_t irq_brk_read = 0;
#endif
void dump_cpu_history(void);
#endif

#define CPU_VAB_MASK    (1 << 24)
#define CPU_RWB_MASK    (1 << 25)
#define CPU_IODEV_MASK  0xFF
#define CASE_READ(addr) (CPU_RWB_MASK | (addr & CPU_IODEV_MASK))
#define CASE_WRIT(addr) (addr & CPU_IODEV_MASK)

static void __isr __attribute__((optimize("O1")))
mem_bus_pio_irq_handler(void)
{
    uint32_t bus_address;
    uint8_t bus_data = 0xEA;

    // In here we bypass the usual SDK calls as needed for performance.
    while (true)
    {
        if (!(MEM_BUS_PIO->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + MEM_BUS_SM)))) // unwound pio_sm_is_rx_fifo_empty()
        {
            // read address and flags
            bus_address = MEM_BUS_PIO->rxf[MEM_BUS_SM];

            if (!(bus_address & CPU_VAB_MASK)) // act only when CPU provides valid address on bus
            {
#ifdef MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH
                if (main_active() && mem_cpu_address_bus_history_index < MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH)
                {
                    mem_cpu_address_bus_history[mem_cpu_address_bus_history_index++] = bus_address;
                }
#endif

                if (bus_address & CPU_RWB_MASK)
                {
                    // CPU is reading
                }
                else
                {
                    // CPU is writing - pull D0-7 from PIO FIFO
                    while ((MEM_BUS_PIO->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + MEM_BUS_SM))))
                    {
                        // tight_loop_contents()
                    }
                    bus_data = (uint8_t)MEM_BUS_PIO->rxf[MEM_BUS_SM];
                }

                // I/O area access
                if ((bus_address & 0xFFFE00) == 0x00FE00)
                {
                    // ------ FFF0 - FFFF ------
                    if ((bus_address & 0xFFF0) == 0xFFF0)
                        switch (bus_address & (CPU_IODEV_MASK | CPU_RWB_MASK))
                        {
                        case CASE_WRIT(0xFFF0): // xstack
                            // if (xstack_ptr)
                            //     xstack[--xstack_ptr] = bus_data;
                            // API_STACK = xstack[xstack_ptr];
                            break;
                        case CASE_READ(0xFFF0): // xstack
                            // if (xstack_ptr < XSTACK_SIZE)
                            //     ++xstack_ptr;
                            // API_STACK = xstack[xstack_ptr];
                            break;

                        case CASE_WRIT(0xFFF1): // OS function call

                            // api_return_blocked();
                            // if (bus_data == 0x00) // zxstack()
                            // {
                            //     API_STACK = 0;
                            //     xstack_ptr = XSTACK_SIZE;
                            //     api_return_ax(0);
                            // }
                            // else
                            if (bus_data == 0xFF) // exit()
                            {
                                gpio_put(CPU_RESB_PIN, false);
                                main_stop();
                            }
                            break;

                        case CASE_READ(0xFFF4): // COP_L
                        case CASE_READ(0xFFF5): // COP_H
                        case CASE_READ(0xFFF8): // ABORTB_L
                        case CASE_READ(0xFFF9): // ABORTB_H
                        case CASE_READ(0xFFFA): // NMIB_L
                        case CASE_READ(0xFFFB): // NMIB_H
                        case CASE_READ(0xFFFC): // RESETB_l
                        case CASE_READ(0xFFFD): // RESETB_H
                        case CASE_READ(0xFFFE): // IRQB/BRK_L
                        case CASE_READ(0xFFFF): // IRQB/BRK_H
                            MEM_BUS_PIO->txf[MEM_BUS_SM] = REGS(bus_address);
#ifdef ABORT_ON_IRQ_BRK_READ
                            if ((bus_address & 0xFFFFFF) == 0xFFFF)
                            {
                                if (++irq_brk_read >= ABORT_ON_IRQ_BRK_READ)
                                {
                                    printf("\nIRQ/BRK vector read (%d) - aborting...\n", irq_brk_read);
                                    gpio_put(CPU_RESB_PIN, false);
                                    main_stop();
                                    dump_cpu_history();
                                }
                            }
#endif
                            break;
                        case CASE_WRIT(0xFFF4): // COP_L
                        case CASE_WRIT(0xFFF5): // COP_H
                        case CASE_WRIT(0xFFF8): // ABORTB_L
                        case CASE_WRIT(0xFFF9): // ABORTB_H
                        case CASE_WRIT(0xFFFA): // NMIB_L
                        case CASE_WRIT(0xFFFB): // NMIB_H
                        case CASE_WRIT(0xFFFC): // RESETB_l
                        case CASE_WRIT(0xFFFD): // RESETB_H
                        case CASE_WRIT(0xFFFE): // IRQB/BRK_L
                        case CASE_WRIT(0xFFFF): // IRQB/BRK_H
                            REGS(bus_address) = bus_data;
                            break;

                        default:
                            if (bus_address & CPU_RWB_MASK)
                            {
                                // CPU is waiting for some data - push NOP
                                MEM_BUS_PIO->txf[MEM_BUS_SM] = 0xEA;
                            }
                        }
                    // ------ FFE0 - FFEF ------
                    if ((bus_address & 0xFFF0) == 0xFFE0)
                        switch (bus_address & (CPU_IODEV_MASK | CPU_RWB_MASK))
                        {
                        case CASE_READ(0xFFE1): // UART Rx
                        {
                            int ch = cpu_rx_char;
                            if (ch >= 0)
                            {
                                REGS(0xFFE1) = (uint8_t)ch;
                                cpu_rx_char = -1;
                            }
                            else
                            {
                                REGS(0xFFE1) = 0;
                            }
                            MEM_BUS_PIO->txf[MEM_BUS_SM] = REGS(0xFFE1);
                            break;
                        }
                        case CASE_WRIT(0xFFE1): // UART Tx
                            if (com_tx_writable())
                                com_tx_write(bus_data);
                            break;
                        case CASE_READ(0xFFE0): // UART Tx/Rx flow control
                        {
                            int ch = cpu_rx_char;
                            if (ch >= 0)
                                REGS(0xFFE0) |= 0b01000000;
                            else
                                REGS(0xFFE0) &= ~0b01000000;
                            if (com_tx_writable())
                                REGS(0xFFE0) |= 0b10000000;
                            else
                                REGS(0xFFE0) &= ~0b10000000;
                            MEM_BUS_PIO->txf[MEM_BUS_SM] = REGS(0xFFE0);
                            break;
                        }
                        case CASE_WRIT(0xFFE0): // UART Tx/Rx flow control
                            REGS(0xFFE0) = bus_data;
                            break;

                        case CASE_READ(0xFFE3): // Random Number Generator
                        case CASE_READ(0xFFE2): // Two bytes to allow 16 bit instructions
                        {
                            MEM_BUS_PIO->txf[MEM_BUS_SM] = get_rand_32();
                            break;
                        }

                        case CASE_READ(0xFFE4): // COP_L
                        case CASE_READ(0xFFE5): // COP_H
                        case CASE_READ(0xFFE6): // BRK_L
                        case CASE_READ(0xFFE7): // BRK_H
                        case CASE_READ(0xFFE8): // ABORTB_L
                        case CASE_READ(0xFFE9): // ABORTB_H
                        case CASE_READ(0xFFEA): // NMIB_L
                        case CASE_READ(0xFFEB): // NMIB_H
                        case CASE_READ(0xFFEE): // IRQB_L
                        case CASE_READ(0xFFEF): // IRQB_H
                            MEM_BUS_PIO->txf[MEM_BUS_SM] = REGS(bus_address);
                            break;
                        case CASE_WRIT(0xFFE4): // COP_L
                        case CASE_WRIT(0xFFE5): // COP_H
                        case CASE_WRIT(0xFFE6): // BRK_L
                        case CASE_WRIT(0xFFE7): // BRK_H
                        case CASE_WRIT(0xFFE8): // ABORTB_L
                        case CASE_WRIT(0xFFE9): // ABORTB_H
                        case CASE_WRIT(0xFFEA): // NMIB_L
                        case CASE_WRIT(0xFFEB): // NMIB_H
                        case CASE_WRIT(0xFFEE): // IRQB_L
                        case CASE_WRIT(0xFFEF): // IRQB_H
                            REGS(bus_address) = bus_data;
                            break;

                        default:
                            if (bus_address & CPU_RWB_MASK)
                            {
                                // CPU is waiting for some data - push NOP
                                MEM_BUS_PIO->txf[MEM_BUS_SM] = 0xEA;
                            }
                        }
                    // ------ FFD0 - FFDF ------
                    if ((bus_address & 0xFFF0) == 0xFFD0)
                        switch (bus_address & (CPU_IODEV_MASK | CPU_RWB_MASK))
                        {
                            // DMA - FFD0 - FFD9
                            // FS  - FFDA - FFDD

                        default:
                            if (bus_address & CPU_RWB_MASK)
                            {
                                // CPU is waiting for some data - push NOP
                                MEM_BUS_PIO->txf[MEM_BUS_SM] = 0xEA;
                            }
                        }
                    // ------ FFC0 - FFCF ------
                    if ((bus_address & 0xFFF0) == 0xFFC0)
                        switch (bus_address & (CPU_IODEV_MASK | CPU_RWB_MASK))
                        {
                        // math accelerator - OPERA, OPERB
                        case CASE_WRIT(0xFFC0):
                        case CASE_WRIT(0xFFC1):
                        case CASE_WRIT(0xFFC2):
                        case CASE_WRIT(0xFFC3):
                            REGS(bus_address) = bus_data;
                            break;
                        case CASE_READ(0xFFC0):
                        case CASE_READ(0xFFC1):
                        case CASE_READ(0xFFC2):
                        case CASE_READ(0xFFC3):
                            MEM_BUS_PIO->txf[MEM_BUS_SM] = REGS(bus_address);
                            break;
                        // OPERA * OPERB
                        case CASE_READ(0xFFC4):
                        case CASE_READ(0xFFC5):
                            MEM_BUS_PIO->txf[MEM_BUS_SM] = 0; // FIXME: implement
                            break;
                        // Signed OPERA / unsigned OPERB
                        case CASE_READ(0xFFC6):
                        case CASE_READ(0xFFC7):
                            MEM_BUS_PIO->txf[MEM_BUS_SM] = 0; // FIXME: implement
                            break;

                        // monotonic clock
                        case CASE_READ(0xFFC8):
                        case CASE_READ(0xFFC9):
                        case CASE_READ(0xFFCA):
                        case CASE_READ(0xFFCB):
                        case CASE_READ(0xFFCC):
                        case CASE_READ(0xFFCD):
                        {
                            uint64_t us = to_us_since_boot(get_absolute_time());
                            MEM_BUS_PIO->txf[MEM_BUS_SM] = ((uint8_t *)&us)[bus_address & 0x07];
                            break;
                        }

                        default:
                            if (bus_address & CPU_RWB_MASK)
                            {
                                // CPU is waiting for some data - push NOP
                                MEM_BUS_PIO->txf[MEM_BUS_SM] = 0xEA;
                            }
                        }
                }
                else
                {
                    // normal memory access
                    if (bus_address & CPU_RWB_MASK)
                    { // CPU is reading
                        // Push 1 byte from RAM to PIO tx FIFO
                        MEM_BUS_PIO->txf[MEM_BUS_SM] = psram[bus_address & 0xFFFFFF];
                    }
                    else
                    { // CPU is writing
                        // Store bus D0-7 to RAM
                        psram[bus_address & 0xFFFFFF] = bus_data;
                    }
                }
            }
        }
        else // exit if there was nothing to read
        {
            // Clear the interrupt request
            pio_interrupt_clear(MEM_BUS_PIO, MEM_BUS_PIO_IRQ);
            return;
        }
    }
}

void ria_trigger_irq(void)
{
    if (irq_enabled & 0x01)
        gpio_put(CPU_IRQB_PIN, false);
}

static void mem_bus_irq_init(void)
{
    // drive irq pin
    gpio_init(CPU_IRQB_PIN);
    gpio_put(CPU_IRQB_PIN, true);
    gpio_set_dir(CPU_IRQB_PIN, true);
}

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
    pio_set_irq1_source_enabled(MEM_BUS_PIO, pis_sm0_rx_fifo_not_empty, true);
    pio_interrupt_clear(MEM_BUS_PIO, MEM_BUS_PIO_IRQ);
    pio_sm_init(MEM_BUS_PIO, MEM_BUS_SM, offset, &config);
    irq_set_exclusive_handler(PIO_IRQ_NUM(MEM_BUS_PIO, MEM_BUS_PIO_IRQ), mem_bus_pio_irq_handler);
    irq_set_enabled(PIO_IRQ_NUM(MEM_BUS_PIO, MEM_BUS_PIO_IRQ), true);
    pio_sm_set_enabled(MEM_BUS_PIO, MEM_BUS_SM, true);
}

void bus_init(void)
{
    // safety check for compiler alignment
    assert(!((uintptr_t)regs & 0x1F));

    // Lower CPU0 on crossbar by raising others
    bus_ctrl_hw->priority |=              //
        BUSCTRL_BUS_PRIORITY_DMA_R_BITS | //
        BUSCTRL_BUS_PRIORITY_DMA_W_BITS | //
        BUSCTRL_BUS_PRIORITY_PROC1_BITS;

    // Adjustments for GPIO performance. Important!
    for (int i = BUS_PIN_BASE; i < BUS_PIN_BASE + BUS_DATA_PINS_USED; ++i)
    {
        pio_gpio_init(MEM_BUS_PIO, i);
        gpio_set_pulls(i, false, false);
        gpio_set_input_hysteresis_enabled(i, false);
        hw_set_bits(&MEM_BUS_PIO->input_sync_bypass, 1u << i);
    }
    for (int i = BUS_CTL_PIN_BASE; i < BUS_CTL_PIN_BASE + BUS_CTL_PINS_USED; ++i)
    {
        pio_gpio_init(MEM_BUS_PIO, i);
        gpio_set_pulls(i, false, false);
        gpio_set_input_hysteresis_enabled(i, false);
        hw_set_bits(&MEM_BUS_PIO->input_sync_bypass, 1u << i);
    }

    // the inits
    mem_bus_irq_init();
    mem_bus_pio_init();
}

void bus_run(void)
{
}

void bus_stop(void)
{
    irq_enabled = false;
    gpio_put(CPU_IRQB_PIN, true);
#ifdef MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH
    mem_cpu_address_bus_history_index = 0;
#ifdef ABORT_ON_IRQ_BRK_READ
    irq_brk_read = 0;
#endif
#endif
}

#ifdef MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH
void dump_cpu_history(void)
{
    for (size_t i = 0; mem_cpu_address_bus_history_index; i++, mem_cpu_address_bus_history_index--)
    {
        printf("CPU: 0x%06lX %s\n",
               mem_cpu_address_bus_history[i] & 0xFFFFFF,
               mem_cpu_address_bus_history[i] & CPU_RWB_MASK ? "R" : "w");
    }
}
#endif

void bus_task(void)
{
#ifdef MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH
    if (mem_cpu_address_bus_history_index >= MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH)
#ifdef MEM_CPU_ADDRESS_BUS_DUMP
        dump_cpu_history();
#else
        mem_cpu_address_bus_history_index = 0;
#endif
#endif
}
