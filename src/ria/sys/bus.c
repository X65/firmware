/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "bus.h"
#include "api/api.h"
#include "bus.pio.h"
#include "cgia/cgia.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/structs/bus_ctrl.h"
#include "main.h"
#include "pico/rand.h"
#include "pico/time.h"
#include "sys/com.h"
#include "sys/cpu.h"
#include "sys/ext.h"
#include "sys/mem.h"

#include <stdbool.h>
#include <stdio.h>

// bus.pio requires PIO clock at 111MHz
// 336MHz / 111MHz => ~3x divider
// FIXME: BUT (3) will spin the CPU so fast, that the ARM Core0 will do nothing
// but service the PIO interrupts. :-(
// So we are setting it to (5) which is lowest that works.
// TODO: Add yet another ARM CPU core to RP micro-controller.
#define MEM_BUS_PIO_CLKDIV_INT   (5)
#define MEM_BUS_PIO_CLKDIV_FRAC8 (0)

volatile uint8_t
    __attribute__((aligned(4)))
    __scratch_y("")
        __regs[0x40];

static volatile bool irq_enabled = false;

static enum state {
    BUS_PENDING_NOTHING,
    BUS_PENDING_READ,
    BUS_PENDING_WRITE,
} volatile bus_pending_operation;
static uint32_t bus_pending_addr;
static uint8_t bus_pending_data;

// #define MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH 50
// // #define MEM_CPU_ADDRESS_BUS_DUMP
// #define ABORT_ON_IRQ_BRK_READ              2
#ifdef MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH
#include <stdio.h>
static uint32_t mem_cpu_address_bus_history[MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH];
static uint8_t mem_cpu_address_bus_history_index = 0;
#ifdef ABORT_ON_IRQ_BRK_READ
static uint8_t irq_brk_read = 0;
#endif
void dump_cpu_history(void);
#endif

#define CPU_VAB_MASK    (1 << 24)
#define CPU_RWB_MASK    (1 << 25)
#define CPU_IODEV_MASK  0xFF
#define CASE_READ(addr) (CPU_RWB_MASK | (addr & CPU_IODEV_MASK))
#define CASE_WRIT(addr) (addr & CPU_IODEV_MASK)

static void __isr __attribute__((optimize("O3")))
mem_bus_pio_irq_handler(void)
{
    /*
        BUS address is read by PIO in the following chunks:
        [. . . . . . RWB VAB BA7-BA0] [A15-A8] [A7-A0]
        Use above CPU_*_MASK to extract bits.
    */
    uint32_t bus_address;
    uint8_t bus_data = 0xEA; // NOP

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
                        tight_loop_contents();
                    }
                    bus_data = (uint8_t)MEM_BUS_PIO->rxf[MEM_BUS_SM];
                }

                // I/O area access
                if ((bus_address & 0xFFFFC0) == 0x00FFC0) // RP816 RIA registers
                {
                    // ------ FFF0 - FFFF ------ (API, EXT CTL)
                    if ((bus_address & 0xFFF0) == 0xFFF0)
                        switch (bus_address & (CPU_IODEV_MASK | CPU_RWB_MASK))
                        {
                        case CASE_WRIT(0xFFF0): // xstack
                            if (xstack_ptr)
                                xstack[--xstack_ptr] = bus_data;
                            break;
                        case CASE_READ(0xFFF0): // xstack
                            MEM_BUS_PIO->txf[MEM_BUS_SM] = xstack[xstack_ptr];
                            if (xstack_ptr < XSTACK_SIZE)
                                ++xstack_ptr;
                            break;
                        case CASE_READ(0xFFF1): // API return value
                            MEM_BUS_PIO->txf[MEM_BUS_SM] = API_OP;
                            break;
                        case CASE_WRIT(0xFFF1): // API call
                            API_OP = bus_data;
                            api_set_regs_blocked();
                            if (bus_data == API_OP_ZXSTACK)
                            {
                                API_STACK = 0;
                                xstack_ptr = XSTACK_SIZE;
                                api_return_ax(0);
                            }
                            else if (bus_data == API_OP_HALT)
                            {
                                gpio_put(CPU_RESB_PIN, false);
                                main_stop();
                            }
                            break;
                        case CASE_READ(0xFFF2): // API ERRNO
                            MEM_BUS_PIO->txf[MEM_BUS_SM] = API_ERRNO;
                            break;
                        case CASE_WRIT(0xFFF2): // ignore write
                            break;
                        case CASE_READ(0xFFF3): // API BUSY
                            MEM_BUS_PIO->txf[MEM_BUS_SM] = API_BUSY;
                            break;
                        case CASE_WRIT(0xFFF3): // ignore write
                            break;

                        case CASE_READ(0xFFF6): // EXTIO
                            MEM_BUS_PIO->txf[MEM_BUS_SM] = 0x00;
                            break;
                        case CASE_WRIT(0xFFF6): // EXTIO
                            REGS(bus_address) = bus_data;
                            break;
                        case CASE_READ(0xFFF7): // EXTMEM
                            MEM_BUS_PIO->txf[MEM_BUS_SM] = 0x00;
                            break;
                        case CASE_WRIT(0xFFF7): // EXTMEM
                            REGS(bus_address) = bus_data;
                            break;

                        // COP ABORTB NMIB RESETB IRQB/BRK
                        default:
                            if (bus_address & CPU_RWB_MASK)
                            {
                                MEM_BUS_PIO->txf[MEM_BUS_SM] = REGS(bus_address);
                            }
                            else
                            {
                                REGS(bus_address) = bus_data;
                            }
                        }
                    // ------ FFE0 - FFEF ------ (UART, RNG, IRQ CTL)
                    else if ((bus_address & 0xFFF0) == 0xFFE0)
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
                        case CASE_READ(0xFFE2): // Two bytes to allow 16 bit values
                        {
                            MEM_BUS_PIO->txf[MEM_BUS_SM] = get_rand_32();
                            break;
                        }

                        case CASE_READ(0xFFEC): // IRQ_STATUS
                            MEM_BUS_PIO->txf[MEM_BUS_SM] = 0x00;
                            break;
                        case CASE_WRIT(0xFFEC): // IRQ_STATUS
                            REGS(bus_address) = bus_data;
                            break;
                        case CASE_READ(0xFFED): // IRQ_ENABLE
                            MEM_BUS_PIO->txf[MEM_BUS_SM] = 0x00;
                            break;
                        case CASE_WRIT(0xFFED): // IRQ_ENABLE
                            REGS(bus_address) = bus_data;
                            break;

                        // COP BRK ABORTB NMIB IRQB
                        default:
                            if (bus_address & CPU_RWB_MASK)
                            {
                                MEM_BUS_PIO->txf[MEM_BUS_SM] = REGS(bus_address);
                            }
                            else
                            {
                                REGS(bus_address) = bus_data;
                            }
                        }
                    // ------ FFD0 - FFDF ------ (DMA, FS)
                    else if ((bus_address & 0xFFF0) == 0xFFD0)
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
                    // ------ FFC0 - FFCF ------ (MUL/DIV, TOD)
                    else if ((bus_address & 0xFFF0) == 0xFFC0)
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
                        // OPERA * OPERB - multiplication accelerator
                        case CASE_READ(0xFFC4):
                        case CASE_READ(0xFFC5):
                        {
                            uint16_t mul = REGSW(0xFFC0) * REGSW(0xFFC2);
                            MEM_BUS_PIO->txf[MEM_BUS_SM] = (bus_address & 1) ? (mul >> 8) : (mul & 0xFF);
                        }
                        break;
                        // Signed OPERA / unsigned OPERB - division accelerator
                        case CASE_READ(0xFFC6):
                        case CASE_READ(0xFFC7):
                        {
                            const int16_t oper_a = (int16_t)REGSW(0xFFC0);
                            const uint16_t oper_b = (uint16_t)REGSW(0xFFC2);
                            uint16_t div = oper_b ? (oper_a / oper_b) : 0xFFFF;
                            MEM_BUS_PIO->txf[MEM_BUS_SM] = (bus_address & 1) ? (div >> 8) : (div & 0xFF);
                        }
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
                // ------ FF80 - FF87 ------ (GPIO)
                else if ((bus_address & 0xFFF8) == 0xFF80)
                {
                    uint8_t reg = (uint8_t)(bus_address & 0x7);
                    if (bus_address & CPU_RWB_MASK)
                    { // CPU is reading
                        uint8_t val = ext_reg_read(IOE_I2C_ADDRESS, reg);
                        MEM_BUS_PIO->txf[MEM_BUS_SM] = val;
                    }
                    else
                    { // CPU is writing
                        ext_reg_write(IOE_I2C_ADDRESS, reg, bus_data);
                    }
                }
                // ------ FF00 - FF7F ------ (CGIA registers)
                else if ((bus_address & 0xFFFF80) == 0x00FF00)
                {
                    if (bus_address & CPU_RWB_MASK)
                    { // CPU is reading
                        MEM_BUS_PIO->txf[MEM_BUS_SM] = cgia_reg_read((uint8_t)bus_address);
                    }
                    else
                    { // CPU is writing
                        cgia_reg_write((uint8_t)bus_address, bus_data);
                    }
                }
                else
                {
                    const uint32_t addr = bus_address & 0xFFFFFF;
                    bool cpu_is_reading = bus_address & CPU_RWB_MASK;

                    if (!MEM_CAN_ACCESS_ADDR(addr))
                    {
                        // something acquired a PSRAM bank, so we need to
                        // stop the PIO to halt the CPU
                        // and configure a pending operation
                        pio_sm_set_enabled(MEM_BUS_PIO, MEM_BUS_SM, false);

                        bus_pending_operation = cpu_is_reading ? BUS_PENDING_READ : BUS_PENDING_WRITE;
                        bus_pending_addr = addr;
                        bus_pending_data = bus_data;

                        // Clear the interrupt request and exit
                        pio_interrupt_clear(MEM_BUS_PIO, MEM_BUS_PIO_IRQ);
                        return;
                    }

                    // normal memory access
                    if (cpu_is_reading)
                    { // CPU is reading
                        // Push 1 byte from RAM to PIO tx FIFO
                        MEM_BUS_PIO->txf[MEM_BUS_SM] = mem_read_psram(addr);
                    }
                    else
                    { // CPU is writing
                        // Store bus D0-7 to RAM
                        mem_write_psram(addr, bus_data);
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
        gpio_put(RIA_IRQB_PIN, false);
}

static void mem_bus_int_init(void)
{
    // drive IRQ pin
    gpio_init(RIA_IRQB_PIN);
    gpio_set_dir(RIA_IRQB_PIN, true);
    gpio_set_pulls(RIA_IRQB_PIN, false, false);
    gpio_put(RIA_IRQB_PIN, true);
    // drive NMI pin (used by CGIA only)
    gpio_init(RIA_NMIB_PIN);
    gpio_set_dir(RIA_NMIB_PIN, true);
    gpio_set_pulls(RIA_NMIB_PIN, false, false);
    gpio_put(RIA_NMIB_PIN, true);
}

static void mem_bus_intctl_init(void)
{
    // drive INT_CTL pin
    gpio_init(INT_CTL_EN_PIN);
    gpio_set_dir(INT_CTL_EN_PIN, true);
    gpio_put(INT_CTL_EN_PIN, true);
}

static void mem_bus_pio_init(void)
{
    // PIO to manage PHI2 clock and 65816 address/data bus
    uint offset = pio_add_program(MEM_BUS_PIO, &mem_bus_program);
    pio_sm_config config = mem_bus_program_get_default_config(offset);
    sm_config_set_clkdiv_int_frac(&config, MEM_BUS_PIO_CLKDIV_INT, MEM_BUS_PIO_CLKDIV_FRAC8);
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
    pio_sm_set_enabled(MEM_BUS_PIO, MEM_BUS_SM, false);
}

void bus_init(void)
{
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
    mem_bus_int_init();
    mem_bus_intctl_init();
    mem_bus_pio_init();

    bus_pending_operation = BUS_PENDING_NOTHING;
}

void bus_run(void)
{
    pio_sm_set_enabled(MEM_BUS_PIO, MEM_BUS_SM, true);
}

void bus_stop(void)
{
    pio_sm_set_enabled(MEM_BUS_PIO, MEM_BUS_SM, false);
    irq_enabled = false;
    gpio_put(RIA_IRQB_PIN, true);
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
    if (bus_pending_operation != BUS_PENDING_NOTHING
        && MEM_CAN_ACCESS_ADDR(bus_pending_addr))
    {
        // PSRAM bank acquire was released,
        // so we can now do a pending operation
        if (bus_pending_operation == BUS_PENDING_READ)
        {
            MEM_BUS_PIO->txf[MEM_BUS_SM] = mem_read_psram(bus_pending_addr);
        }
        if (bus_pending_operation == BUS_PENDING_WRITE)
        {
            mem_write_psram(bus_pending_addr, bus_pending_data);
        }
        // and re-enable CPU bus PIO
        bus_pending_operation = BUS_PENDING_NOTHING;
        pio_sm_set_enabled(MEM_BUS_PIO, MEM_BUS_SM, true);
    }

#ifdef MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH
    if (mem_cpu_address_bus_history_index >= MEM_CPU_ADDRESS_BUS_HISTORY_LENGTH)
#ifdef MEM_CPU_ADDRESS_BUS_DUMP
        dump_cpu_history();
#else
        mem_cpu_address_bus_history_index = 0;
#endif
#endif
}

void bus_print_status(void)
{
    printf("CPU : ~%.2fMHz\n", (float)SYS_CLK_HZ / MEM_BUS_PIO_CLKDIV_INT / (WRITE_DELAY * 2.2) / MHZ);
}
