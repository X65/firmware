/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "bus.h"
#include "api/api.h"
#include "api/oem.h"
#include "bus.pio.h"
#include "cgia/cgia.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/structs/bus_ctrl.h"
#include "hid/kbd.h"
#include "hid/mou.h"
#include "hid/pad.h"
#include "main.h"
#include "pico/rand.h"
#include "pico/time.h"
#include "sys/aud.h"
#include "sys/com.h"
#include "sys/cpu.h"
#include "sys/cia.h"
#include "sys/ext.h"
#include "sys/mem.h"
#include "sys/mem_l2.h"
#include "sys/pcm.h"

#include <stdbool.h>
#include <stdio.h>

// bus.pio requires PIO clock at 111MHz
// 336MHz / 111MHz => ~3x divider
// FIXME: BUT (3) will spin the CPU so fast, that the ARM Core0 will do nothing
// but service the PIO interrupts. :-(
// So we are setting it to (5) which is lowest that works.
// TODO: Add yet another ARM CPU core to RP micro-controller. ;-)
#define MEM_BUS_PIO_CLKDIV_INT   (5)
#define MEM_BUS_PIO_CLKDIV_FRAC8 (0)

// NOTE: these timings are CPU clock dependant!
#define IRQ_CTL_DELAY (70)

volatile uint8_t
    __attribute__((aligned(4)))
    __scratch_y("")
        __regs[0x40];

// RIA interrupt: IRQ_ENABLE ($FFEC) bit 0 gates the CIA timers IRQ
static volatile uint8_t irq_enable = 0;
static volatile bool cia_irq_line = false;

static inline void bus_update_irq(void)
{
    gpio_put(RIA_IRQB_PIN, !(cia_irq_line && (irq_enable & 0x01)));
}

void bus_set_cia_irq(bool asserted)
{
    cia_irq_line = asserted;
    bus_update_irq();
}

// HID device selected by a write to $FFB0: [AAAA DDDD]
// D - device type, A - device index
#define RIA_HID_DEV_KEYBOARD 0x00
#define RIA_HID_DEV_MOUSE    0x01
#define RIA_HID_DEV_GAMEPAD  0x02
static uint8_t hid_dev = 0;

// gen1 RGB LEDs hang off the ESP32 and the RIA drives no buzzer:
// these registers only hold values
static uint8_t rgb_regs[8];
static uint8_t buz_regs[4];
// API_REJECTED or 0, owned by the bus ISR so op results can't clear it
static uint8_t api_rejected = 0;

static enum state {
    BUS_PENDING_NOTHING,
    BUS_PENDING_DELAY,
} volatile bus_pending_operation;
static uint32_t bus_pending_delay;

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

static const uint8_t BIT8_MASK[] = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};

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
                    uint8_t data = 0xFF;
                    switch (bus_address & (CPU_IODEV_MASK | CPU_RWB_MASK))
                    {
                    // ------ FFC0 - FFCF ------ (MUL/DIV, TOD)
                    // OPERA * OPERB - multiplication accelerator
                    case CASE_READ(0xFFC4):
                    case CASE_READ(0xFFC5):
                    case CASE_READ(0xFFC6):
                    case CASE_READ(0xFFC7):
                    {
                        const uint32_t mul = (uint32_t)REGSW(0xFFC0) * REGSW(0xFFC2);
                        data = ((uint8_t *)&mul)[bus_address & 0x03];
                        break;
                    }
                    // Signed OPERA / unsigned OPERB - division accelerator
                    case CASE_READ(0xFFC8):
                    case CASE_READ(0xFFC9):
                    {
                        const int16_t oper_a = (int16_t)REGSW(0xFFC0);
                        const uint16_t oper_b = (uint16_t)REGSW(0xFFC2);
                        const uint16_t div = oper_b ? (uint16_t)(oper_a / oper_b) : 0xFFFF;
                        data = ((uint8_t *)&div)[bus_address & 0x01];
                        break;
                    }
                    // monotonic clock, microseconds, 48 bits
                    case CASE_READ(0xFFCA):
                    case CASE_READ(0xFFCB):
                    case CASE_READ(0xFFCC):
                    case CASE_READ(0xFFCD):
                    case CASE_READ(0xFFCE):
                    case CASE_READ(0xFFCF):
                    {
                        const uint64_t us = to_us_since_boot(get_absolute_time());
                        data = ((uint8_t *)&us)[(bus_address - 2) & 0x07];
                        break;
                    }

                    // ------ FFD0 - FFDF ------ (DMA, FS) plain registers

                    // ------ FFE0 - FFEF ------ (UART, RNG, IRQ CTL)
                    case CASE_READ(0xFFE0): // UART Tx/Rx flow control
                        data = 0;
                        if (cpu_rx_char >= 0)
                            data |= 0b01000000;
                        if (com_tx_writable())
                            data |= 0b10000000;
                        break;
                    case CASE_READ(0xFFE1): // UART Rx
                    {
                        const int ch = cpu_rx_char;
                        if (ch >= 0)
                        {
                            data = (uint8_t)ch;
                            cpu_rx_char = -1;
                        }
                        break;
                    }
                    case CASE_WRIT(0xFFE1): // UART Tx
                        if (com_tx_writable())
                            com_tx_write(bus_data);
                        break;
                    case CASE_READ(0xFFE2): // Random Number Generator
                    case CASE_READ(0xFFE3): // Two bytes to allow 16 bit values
                        data = (uint8_t)get_rand_32();
                        break;
                    case CASE_READ(0xFFEC): // IRQ_ENABLE
                        data = irq_enable;
                        break;
                    case CASE_WRIT(0xFFEC): // IRQ_ENABLE
                        irq_enable = bus_data & 0x01;
                        bus_update_irq();
                        break;
                    case CASE_READ(0xFFED): // IRQ_STATUS
                    {
                        // 1. stop BUS PIO
                        MEM_BUS_PIO->irq_force = (1u << GATE_IRQ); // raise gating IRQ
                        // 2. turn off all BUS buffers
                        gpio_set_outover(BUS_BE0_PIN, GPIO_OVERRIDE_HIGH);
                        gpio_set_outover(BUS_BE1_PIN, GPIO_OVERRIDE_HIGH);
                        // 3. turn on INT CTL buffer
                        gpio_put(INT_CTL_EN_PIN, false);
                        // 4. push fake data to trigger PIO run
                        MEM_BUS_PIO->txf[MEM_BUS_SM] = 0x5a;
                        // 5. wait until PIO stops reading
                        while (gpio_get(BUS_DIR_PIN))
                            tight_loop_contents();
                        // 6. turn off INT CTL buffer
                        gpio_put(INT_CTL_EN_PIN, true);
                        // 7. give back BE0 and BE1 to PIO
                        gpio_set_outover(BUS_BE0_PIN, GPIO_OVERRIDE_NORMAL);
                        gpio_set_outover(BUS_BE1_PIN, GPIO_OVERRIDE_NORMAL);
                        // 8. schedule BUS PIO restart after some delay
                        bus_pending_operation = BUS_PENDING_DELAY;
                        bus_pending_delay = IRQ_CTL_DELAY;
                        // 9. Clear the interrupt request and exit
                        pio_interrupt_clear(MEM_BUS_PIO, MEM_BUS_PIO_IRQ);
                        return;
                    }
                    case CASE_WRIT(0xFFED): // IRQ_STATUS is read only
                        break;

                    // ------ FFF0 - FFFF ------ (API, EXT CTL)
                    case CASE_WRIT(0xFFF0): // API call
                        if (API_BUSY && bus_data != API_OP_HALT)
                        {
                            // Another op is running: refuse this one
                            // and leave the running op alone.
                            api_rejected = API_REJECTED;
                            break;
                        }
                        api_rejected = 0;
                        api_set_regs_blocked();
                        if (bus_data == API_OP_ZXSTACK)
                        {
                            xstack_ptr = XSTACK_SIZE;
                            api_return_ax(0);
                        }
                        else if (bus_data == API_OP_HALT)
                        {
                            gpio_put(CPU_RESB_PIN, false);
                            main_stop();
                        }
                        else if (bus_data == API_OP_OEM_GET_CHARGEN)
                        {
                            // The X65 emulator finishes this inside the op
                            // write and programs rely on it: the 65816 waits
                            // for the 2 KB blit, it can't overwrite later.
                            oem_api_get_chargen();
                        }
                        else
                        {
                            API_OP = bus_data;
                        }
                        break;
                    case CASE_READ(0xFFF3): // API status
                        data = API_STATUS | api_rejected;
                        break;
                    case CASE_READ(0xFFF2): // xstack
                        data = xstack[xstack_ptr];
                        if (xstack_ptr < XSTACK_SIZE)
                            ++xstack_ptr;
                        break;
                    case CASE_WRIT(0xFFF2): // xstack
                        if (xstack_ptr)
                            xstack[--xstack_ptr] = bus_data;
                        break;

                    // API return value, status, CPU vectors, EXTIO, EXTMEM
                    default:
                        if (bus_address & CPU_RWB_MASK)
                            data = REGS(bus_address);
                        else
                            REGS(bus_address) = bus_data;
                    }
                    if (bus_address & CPU_RWB_MASK)
                        MEM_BUS_PIO->txf[MEM_BUS_SM] = data;
                }
                // ------ FF80 - FFBF ------ (devices mapped by RIA)
                else if ((bus_address & 0xFFFFC0) == 0x00FF80)
                {
                    const uint8_t reg = (uint8_t)bus_address;
                    const bool is_read = bus_address & CPU_RWB_MASK;
                    uint8_t data = 0xFF;
                    if (reg >= 0xB0) // ------ FFB0 - FFBF ------ (HID)
                    {
                        if (is_read)
                            switch (hid_dev & 0x0F)
                            {
                            case RIA_HID_DEV_KEYBOARD:
                                data = kbd_get_reg((hid_dev & 0xF0) | (reg & 0x0F));
                                break;
                            case RIA_HID_DEV_MOUSE:
                                data = mou_get_reg(reg & 0x0F);
                                break;
                            case RIA_HID_DEV_GAMEPAD:
                                data = pad_get_reg(hid_dev >> 4, reg & 0x0F);
                                break;
                            }
                        else if ((reg & 0x0F) == 0x00) // HID SELECT
                            hid_dev = bus_data;
                    }
                    else if (reg >= 0xAC) // ------ FFAC - FFAF ------ (unused)
                    {
                    }
                    else if (reg >= 0xA8) // ------ FFA8 - FFAB ------ (buzzer)
                    {
                        if (is_read)
                            data = buz_regs[reg & 0x03];
                        else
                            buz_regs[reg & 0x03] = bus_data;
                    }
                    else if (reg >= 0xA0) // ------ FFA0 - FFA7 ------ (RGB LEDs)
                    {
                        if (is_read)
                            data = rgb_regs[reg & 0x07];
                        else
                            rgb_regs[reg & 0x07] = bus_data;
                    }
                    else if (reg >= 0x98) // ------ FF98 - FF9F ------ (CIA timers)
                    {
                        if (is_read)
                            data = cia_reg_read(reg & 0x07);
                        else
                            cia_reg_write(reg & 0x07, bus_data);
                    }
                    else // ------ FF80 - FF97 ------ (GPIO extender)
                    {
                        if (is_read)
                            data = ext_reg_read(IOE_I2C_ADDRESS, reg & 0x07);
                        else
                            ext_reg_write(IOE_I2C_ADDRESS, reg & 0x07, bus_data);
                    }
                    if (is_read)
                        MEM_BUS_PIO->txf[MEM_BUS_SM] = data;
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
                // ------ FEC0 - FEFF ------ (SD-1 registers)
                // $FEE3-$FEFE, the SD-1's read-only EQ readback, is the PCM player
                else if ((bus_address & 0xFFFFC0) == 0x00FEC0)
                {
                    const uint8_t reg = bus_address & 0x3F;
                    const bool is_pcm = reg >= PCM_REG_FIRST && reg <= PCM_REG_LAST;
                    if (bus_address & CPU_RWB_MASK)
                    { // CPU is reading
                        MEM_BUS_PIO->txf[MEM_BUS_SM] = is_pcm ? pcm_reg_read(reg)
                                                              : aud_read_fm_register(reg);
                    }
                    else if (is_pcm)
                    { // CPU is writing
                        pcm_reg_write(reg, bus_data);
                    }
                    else
                    {
                        aud_write_fm_register(reg, bus_data);
                    }
                }
                // ------ FC00 - FDFF ------ (EXT I/O registers)
                // The window belongs to the expansion bus out of reset.
                // A set EXTIO ($FFF6) bit hands its 64-byte chunk back to RAM.
                else if ((bus_address & 0xFFFE00) == 0x00FC00 &&
                         !(REGS(0x00FFF6) & BIT8_MASK[(bus_address >> 6) & 0x07]))
                {
                    // same as IRQ_STATUS above
                    MEM_BUS_PIO->irq_force = (1u << GATE_IRQ); // raise gating IRQ
                    gpio_set_outover(BUS_BE0_PIN, GPIO_OVERRIDE_HIGH);
                    gpio_set_outover(BUS_BE1_PIN, GPIO_OVERRIDE_HIGH);

                    // enable EXT I/O
                    gpio_put(EXT_IO_CS_PIN, false);
                    gpio_put(EXT_IO_BE0_PIN, bus_address & (1 << 7));
                    gpio_put(EXT_IO_BE1_PIN, bus_address & (1 << 8));

                    MEM_BUS_PIO->txf[MEM_BUS_SM] = 0x5a;
                    while (gpio_get(BUS_DIR_PIN))
                        tight_loop_contents();

                    // turn off EXT I/O
                    gpio_put(EXT_IO_CS_PIN, true);

                    gpio_set_outover(BUS_BE0_PIN, GPIO_OVERRIDE_NORMAL);
                    gpio_set_outover(BUS_BE1_PIN, GPIO_OVERRIDE_NORMAL);
                    bus_pending_operation = BUS_PENDING_DELAY;
                    bus_pending_delay = IRQ_CTL_DELAY;
                    pio_interrupt_clear(MEM_BUS_PIO, MEM_BUS_PIO_IRQ);
                    return;
                }
                else
                {
                    const uint32_t addr = bus_address & 0xFFFFFF;
                    bool cpu_is_reading = bus_address & CPU_RWB_MASK;

                    // normal memory access
                    if (cpu_is_reading)
                    { // CPU is reading
                        // Push 1 byte from RAM to PIO tx FIFO
                        MEM_BUS_PIO->txf[MEM_BUS_SM] = mem_read_ram_isr(addr);
                    }
                    else
                    { // CPU is writing
                        // Store bus D0-7 to RAM
                        mem_write_ram_isr(addr, bus_data);
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
    // no autopush: bus.pio pushes explicitly where a stall is harmless
    sm_config_set_in_shift(&config, true, false, 32);
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
    MEM_BUS_PIO->irq_force = (1u << GATE_IRQ); // raise gating IRQ
    pio_sm_set_enabled(MEM_BUS_PIO, MEM_BUS_SM, true);
}

static void bus_ext_io_init()
{
    const uint ext_io_pins[] = {
        EXT_IO_CS_PIN,
        EXT_IO_BE0_PIN,
        EXT_IO_BE1_PIN,
    };
    for (size_t i = 0; i < sizeof(ext_io_pins) / sizeof(ext_io_pins[0]); i++)
    {
        uint pin = ext_io_pins[i];
        gpio_init(pin);
        gpio_put(pin, true);
        gpio_set_pulls(pin, false, false);
        gpio_set_dir(pin, true);
    }
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
    bus_ext_io_init();
    mem_bus_pio_init();

    bus_pending_operation = BUS_PENDING_NOTHING;
}

void bus_run(void)
{
    // expansion window routed to the bus, no MMU
    REGS(0xFFF6) = 0;
    REGS(0xFFF7) = 0;
    hid_dev = 0;
    api_rejected = 0;
    MEM_BUS_PIO->irq = (1u << GATE_IRQ); // clear gating IRQ
}

void bus_stop(void)
{
    MEM_BUS_PIO->irq_force = (1u << GATE_IRQ); // raise gating IRQ
    irq_enable = 0;
    bus_update_irq();
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
    if (bus_pending_operation != BUS_PENDING_NOTHING)
    {
        switch (bus_pending_operation)
        {
        case BUS_PENDING_DELAY:
            if (--bus_pending_delay == 0)
            {
                bus_pending_operation = BUS_PENDING_NOTHING;
            }
            break;
        case BUS_PENDING_NOTHING:
            bus_pending_operation = BUS_PENDING_NOTHING;
            break;
        }

        if (bus_pending_operation == BUS_PENDING_NOTHING)
        {
            // re-enable CPU bus PIO
            MEM_BUS_PIO->irq = (1u << GATE_IRQ); // clear gating IRQ
        }
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

// PIO cycles per PHI2 cycle, measured for bus.pio (see WRITE_DELAY)
#define BUS_PIO_CYCLES_PER_PHI2 (WRITE_DELAY * 2.1644)

void bus_print_status(void)
{
    printf("CPU : ~%.2fMHz\n", (float)SYS_CLK_HZ / MEM_BUS_PIO_CLKDIV_INT / BUS_PIO_CYCLES_PER_PHI2 / MHZ);
}

uint16_t bus_get_phi2_khz(void)
{
    return (uint16_t)((float)SYS_CLK_HZ / MEM_BUS_PIO_CLKDIV_INT / BUS_PIO_CYCLES_PER_PHI2 / 1000);
}
