/*
 * Copyright (c) 2023 Rumbledethumps
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/ria.h"
#include "hardware/pio.h"
#include "hardware/structs/bus_ctrl.h"
#include "main.h"
#include "pico/multicore.h"
#include "sys/com.h"
#include "sys/cpu.h"
#include "sys/mem.h"
#include <stdio.h>

static volatile bool irq_enabled;

void ria_trigger_irq(void)
{
    if (irq_enabled & 0x01)
        gpio_put(CPU_IRQB_PIN, false);
}

void ria_run(void)
{
}

void ria_stop(void)
{
    irq_enabled = false;
    gpio_put(CPU_IRQB_PIN, true);
}

void ria_task(void)
{
}

#define MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH 20
static uint32_t mem_cpu_address_bus_history[MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH];
static uint8_t mem_cpu_address_bus_history_index = 0;

#define CPU_VAB_MASK    (1 << 24)
#define CPU_RWB_MASK    (1 << 25)
#define CPU_IODEV_MASK  0xFF
#define CASE_READ(addr) (CPU_RWB_MASK | (addr & CPU_IODEV_MASK))
#define CASE_WRIT(addr) (addr & CPU_IODEV_MASK)
static __attribute__((optimize("O1"))) void act_loop(void)
{
    uint32_t bus_address;
    uint8_t bus_data = 0xEA;

    // In here we bypass the usual SDK calls as needed for performance.
    while (true)
    {
        if (!(MEM_BUS_PIO->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + MEM_BUS_SM)))) // unwound pio_sm_is_rx_fifo_empty()
        {
            bus_address = MEM_BUS_PIO->rxf[MEM_BUS_SM];
            if (!(bus_address & CPU_VAB_MASK)) // act only when CPU provides valid address on bus
            {
                if (main_active() && mem_cpu_address_bus_history_index < MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH)
                {
                    mem_cpu_address_bus_history[mem_cpu_address_bus_history_index++] = bus_address;
                }

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

                if ((bus_address & 0xFFFF00) == 0x00FF00)
                {
                    // I/O area access
                    switch (bus_address & (CPU_IODEV_MASK | CPU_RWB_MASK))
                    {
                    case CASE_WRIT(0xFFF7): // IRQ Enable
                        irq_enabled = bus_data;
                        __attribute__((fallthrough));
                    case CASE_READ(0xFFF7): // IRQ ACK
                        gpio_put(CPU_IRQB_PIN, true);
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
                    case CASE_READ(0xFFF4): // COP_L
                    case CASE_READ(0xFFF5): // COP_H
                    case CASE_READ(0xFFF8): // ABORTB_L
                    case CASE_READ(0xFFF9): // ABORTB_H
                    case CASE_READ(0xFFFA): // NMIB_L
                    case CASE_READ(0xFFFB): // NMIB_H
                    case CASE_READ(0xFFFC): // RESETB_l
                    case CASE_READ(0xFFFD): // RESETB_H
                    case CASE_READ(0xFFFE): // IRQB/BRK_L
                                            // case CASE_READ(0xFFFF): // IRQB/BRK_H
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
    }
}

static void ria_act_init(void)
{
    // multicore_launch_core1(act_loop);
}

void ria_init(void)
{
    // drive irq pin
    gpio_init(CPU_IRQB_PIN);
    gpio_put(CPU_IRQB_PIN, true);
    gpio_set_dir(CPU_IRQB_PIN, true);

    // safety check for compiler alignment
    assert(!((uintptr_t)regs & 0x1F));

    // Lower CPU0 on crossbar by raising others
    bus_ctrl_hw->priority |=              //
        BUSCTRL_BUS_PRIORITY_DMA_R_BITS | //
        BUSCTRL_BUS_PRIORITY_DMA_W_BITS | //
        BUSCTRL_BUS_PRIORITY_PROC1_BITS;

    // the inits
    ria_act_init();
}

void dump_cpu_history(void)
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
