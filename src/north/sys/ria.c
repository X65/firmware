/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/ria.h"
#include "api/api.h"
#include "hw.h"
#include "main.h"
#include "sys/com.h"
#include "sys/cpu.h"
#include "sys/mem.h"
#include "sys/pix.h"
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/pio.h>
#include <littlefs/lfs_util.h>
#include <pico/multicore.h>
#include <pico/rand.h>
#include <pico/stdio.h>
#include <stdint.h>

#if defined(DEBUG_RIA_SYS) || defined(DEBUG_RIA_SYS_RIA)
#include <stdio.h>
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
static inline void DBG(const char *fmt, ...)
{
    (void)fmt;
}
#endif

static volatile bool irq_enabled;

void ria_trigger_irq(void)
{
    if (irq_enabled & 0x01)
        gpio_put(RIA_IRQB_PIN, false);
}

void ria_run(void)
{
}

static int mem_log_idx = 0;
#define MEM_LOG_SIZE 360
static uint32_t mem_addr[MEM_LOG_SIZE];
static uint8_t mem_data[MEM_LOG_SIZE];
static bool mem_dump = false;

void ria_stop(void)
{
    irq_enabled = false;
    gpio_put(RIA_IRQB_PIN, true);
}

#define CPU_VAB_MASK    (1u << 24)
#define CPU_RWB_MASK    (1u << 25)
#define CPU_IODEV_MASK  0xFF
#define CASE_READ(addr) (CPU_RWB_MASK | ((addr & CPU_IODEV_MASK) << 8))
#define CASE_WRIT(addr) ((addr & CPU_IODEV_MASK) << 8)

void ria_task(void)
{
    if (mem_log_idx == MEM_LOG_SIZE || mem_dump)
    {
        for (int i = 0; i < mem_log_idx; i++)
        {
            // printf("%02d: %08lX %c %02X\n", i, mem_addr[i], (mem_addr[i] & CPU_RWB_MASK) ? 'R' : 'W', mem_data[i]);
        }
        mem_log_idx = MEM_LOG_SIZE + 1;
        mem_dump = false;
    }
}

// This becomes unstable every time I tried to get to O3 by trurning off
// specific optimizations. The annoying bit is that different hardware doesn't
// behave the same. I'm giving up and leaving this at O1, which is plenty fast.
__attribute__((optimize("O1"))) static void __no_inline_not_in_flash_func(act_loop)(void)
{
    static uint8_t data;

    // In here we bypass the usual SDK calls as needed for performance.
    while (true)
    {
        if (!(CPU_BUS_PIO->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + CPU_BUS_SM))))
        {
            const uint32_t rw_addr_bus = CPU_BUS_PIO->rxf[CPU_BUS_SM];

            if (rw_addr_bus & CPU_VAB_MASK)
                continue; // skip invalid accesses

            const uint16_t addr = (uint16_t)(rw_addr_bus >> 8);
            const uint8_t bank = (uint8_t)(rw_addr_bus);

            const bool is_read = (rw_addr_bus & CPU_RWB_MASK);

            if (!is_read)
            {
                // CPU is writing - wait and pull from data bus
                while ((CPU_BUS_PIO->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + CPU_BUS_SM))))
                    tight_loop_contents();
                data = (uint8_t)CPU_BUS_PIO->rxf[CPU_BUS_SM];
            }

            // BANK 0 is special - we have I/O devices mmapped here
            if (bank == 0x00 && addr >= 0xFF00)
            {
                // RIA registers
                if (addr >= 0xFFC0)
                {
                    if (is_read)
                        data = regs[addr & 0x3F];
                    else
                        regs[addr & 0x3F] = data;

                    // ------ FFF0 - FFFF ------ (API, EXT CTL)
                    if (addr >= 0xFFF0)
                        switch (rw_addr_bus & (CPU_RWB_MASK | (CPU_IODEV_MASK << 8)))
                        {
                        case CASE_READ(0xFFF3): // API BUSY
                            data = API_BUSY;
                            break;
                        case CASE_READ(0xFFF2): // API ERRNO
                            data = API_ERRNO;
                            break;
                        case CASE_WRIT(0xFFF1): // RIA API function call
                            api_set_regs_blocked();
                            if (data == API_OP_ZXSTACK)
                            {
                                xstack_ptr = XSTACK_SIZE;
                                api_return_ax(0);
                            }
                            else if (data == API_OP_HALT)
                            {
                                gpio_put(CPU_RESB_PIN, false);
                                main_stop();
                            }
                            mem_dump = true;
                            break;
                        case CASE_READ(0xFFF1): // API return value
                            data = API_OP;
                            break;
                        case CASE_WRIT(0xFFF0): // xstack
                            if (xstack_ptr)
                                xstack[--xstack_ptr] = data;
                            break;
                        case CASE_READ(0xFFF0): // xstack
                            data = xstack[xstack_ptr];
                            if (xstack_ptr < XSTACK_SIZE)
                                ++xstack_ptr;
                            break;
                        }
                    // ------ FFE0 - FFEF ------ (UART, RNG, IRQ CTL)
                    else if (addr >= 0xFFE0)
                        switch (rw_addr_bus & (CPU_RWB_MASK | (CPU_IODEV_MASK << 8)))
                        {
                        case CASE_READ(0xFFED): // IRQ_STATUS
                            data = 0xFF;
                            break;
                        case CASE_WRIT(0xFFEC): // IRQ Enable
                            irq_enabled = data;
                            __attribute__((fallthrough));
                        case CASE_READ(0xFFEC): // IRQ ACK
                            gpio_put(RIA_IRQB_PIN, true);
                            break;
                        case CASE_READ(0xFFE3): // Random Number Generator
                        case CASE_READ(0xFFE2): // Two bytes to allow 16 bit values
                        {
                            data = (uint8_t)get_rand_32();
                            break;
                        }

                        case CASE_READ(0xFFE1): // UART Rx
                        {
                            const int ch = com_rx_char;
                            if (ch >= 0)
                            {
                                data = (uint8_t)ch;
                                com_rx_char = -1;
                            }
                            else
                            {
                                data = 0;
                            }
                            break;
                        }
                        case CASE_WRIT(0xFFE1): // UART Tx
                            if (com_tx_writable())
                                com_tx_write(data);
                            break;
                        case CASE_READ(0xFFE0): // UART Tx/Rx flow control
                        {
                            uint8_t status = 0x00;
                            if (com_rx_char >= 0)
                                status |= 0b01000000;
                            else
                                status &= ~0b01000000;
                            if (com_tx_writable())
                                status |= 0b10000000;
                            else
                                status &= ~0b10000000;
                            data = status;
                            break;
                        }
                        }

                    // ------ FFD0 - FFDF ------ (DMA, FS)
                    else if (addr >= 0xFFD0)
                        switch (rw_addr_bus & (CPU_RWB_MASK | (CPU_IODEV_MASK << 8)))
                        {
                        }
                    // ------ FFC0 - FFCF ------ (MUL/DIV, TOD)
                    else if (addr >= 0xFFC0)
                        switch (rw_addr_bus & (CPU_RWB_MASK | (CPU_IODEV_MASK << 8)))
                        {
                        // unused - return 0xFF
                        case CASE_READ(0xFFCF):
                        case CASE_READ(0xFFCE):
                            data = 0xFF;
                            break;

                        // monotonic clock
                        case CASE_READ(0xFFCD):
                        case CASE_READ(0xFFCC):
                        case CASE_READ(0xFFCB):
                        case CASE_READ(0xFFCA):
                        case CASE_READ(0xFFC9):
                        case CASE_READ(0xFFC8):
                        {
                            uint64_t us = to_us_since_boot(get_absolute_time());
                            data = ((uint8_t *)&us)[addr & 0x07];
                            break;
                        }

                        // Signed OPERA / unsigned OPERB - division accelerator
                        case CASE_READ(0xFFC7):
                        case CASE_READ(0xFFC6):
                        {
                            const int16_t oper_a = (int16_t)REGSW(0xFFC0);
                            const uint16_t oper_b = (uint16_t)REGSW(0xFFC2);
                            uint16_t div = oper_b ? (oper_a / oper_b) : 0xFFFF;
                            data = (addr & 1) ? (div >> 8) : (div & 0xFF);
                        }
                        break;
                        // OPERA * OPERB - multiplication accelerator
                        case CASE_READ(0xFFC5):
                        case CASE_READ(0xFFC4):
                        {
                            uint16_t mul = REGSW(0xFFC0) * REGSW(0xFFC2);
                            data = (addr & 1) ? (mul >> 8) : (mul & 0xFF);
                        }
                        break;
                        }
                }
                else
                {
                    // other I/O devices
                    data = 0xFF;
                }

                // handled - move along
                goto act_loop_epilogue;
            }

            // else is "normal" memory access
            const uint32_t ram_addr = (bank << 16) | addr;
            if (is_read)
                data = mem_read_ram(ram_addr);
            else
                mem_write_ram(ram_addr, data);

        act_loop_epilogue:
            if (is_read)
            {
                // CPU is reading - wait for place and push to data bus
                while ((CPU_BUS_PIO->fstat & (1u << (PIO_FSTAT_TXFULL_LSB + CPU_BUS_SM))))
                    tight_loop_contents();
                CPU_BUS_PIO->txf[CPU_BUS_SM] = data;
            }
            if (cpu_active() && mem_log_idx < MEM_LOG_SIZE)
            {
                mem_data[mem_log_idx] = data;
                mem_addr[mem_log_idx++] = rw_addr_bus;
            }
        }
    }
}

void ria_init(void)
{
    // drive irq pin
    gpio_init(RIA_IRQB_PIN);
    gpio_put(RIA_IRQB_PIN, true);
    gpio_set_dir(RIA_IRQB_PIN, true);

    multicore_launch_core1(act_loop);
}

void ria_print_status(void)
{
    const float clk = (float)(clock_get_hz(clk_sys));
    printf("RIA : %.1fMHz\n", clk / MHZ);
}

uint8_t ria_read_mem(uint32_t addr24)
{
    if (addr24 >= 0xFFC0 && addr24 <= 0xFFFF)
    {
        return regs[addr24 & 0x3F];
    }
    else
    {
        // normal memory read
        return mem_read_ram(addr24);
    }
}

void ria_write_mem(uint32_t addr24, uint8_t data)
{
    if (addr24 >= 0xFFC0 && addr24 <= 0xFFFF)
    {
        regs[addr24 & 0x3F] = data;
    }
    else
    {
        // normal memory write
        mem_write_ram(addr24, data);
    }
}

void ria_read_buf(uint32_t addr24)
{
    for (size_t i = 0; i < mbuf_len; ++i, ++addr24)
    {
        mbuf[i] = ria_read_mem(addr24);
    }
}

void ria_write_buf(uint32_t addr24)
{
    for (size_t i = 0; i < mbuf_len; ++i, ++addr24)
    {
        ria_write_mem(addr24, mbuf[i]);
    }
}
