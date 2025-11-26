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
                        case CASE_READ(0xFFF6): // action write
                            // if (rw_pos < rw_end)
                            // {
                            //     if (rw_pos > 0)
                            //     {
                            //         REGS(0xFFF1) = mbuf[rw_pos];
                            //         REGSW(0xFFF3) += 1;
                            //     }
                            //     if (++rw_pos == rw_end)
                            //         REGS(0xFFF6) = 0x00;
                            // }
                            // else
                            // {
                            //     gpio_put(CPU_RESB_PIN, false);
                            //     action_result = RIA_ACTION_RESULT_FINISHED;
                            //     main_stop();
                            // }
                            break;
                        case CASE_WRIT(0xFFFD): // action read
                            // if (rw_pos < rw_end)
                            // {
                            //     REGSW(0xFFF1) += 1;
                            //     mbuf[rw_pos] = data;
                            //     if (++rw_pos == rw_end)
                            //     {
                            //         gpio_put(CPU_RESB_PIN, false);
                            //         action_result = RIA_ACTION_RESULT_FINISHED;
                            //         main_stop();
                            //     }
                            // }
                            break;
                        case CASE_WRIT(0xFFFC): // action verify
                            // if (rw_pos < rw_end)
                            // {
                            //     REGSW(0xFFF1) += 1;
                            //     if (mbuf[rw_pos] != data && action_result < 0)
                            //         action_result = REGSW(0xFFF1) - 1;
                            //     if (++rw_pos == rw_end)
                            //     {
                            //         gpio_put(CPU_RESB_PIN, false);
                            //         if (action_result < 0)
                            //             action_result = RIA_ACTION_RESULT_FINISHED;
                            //         main_stop();
                            //     }
                            // }
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
                    {
                        switch (rw_addr_bus & (CPU_RWB_MASK | (CPU_IODEV_MASK << 8)))
                        {
                        case CASE_WRIT(0xFFEC): // IRQ Enable
                            irq_enabled = data;
                            __attribute__((fallthrough));
                        case CASE_READ(0xFFEC): // IRQ ACK
                            gpio_put(RIA_IRQB_PIN, true);
                            break;
                        // case CASE_WRIT(0xFFEB): // Set XRAM >ADDR1
                        //     REGS(0xFFEB) = data;
                        //     RIA_RW1 = xram[RIA_ADDR1];
                        //     break;
                        // case CASE_WRIT(0xFFEA): // Set XRAM <ADDR1
                        //     REGS(0xFFEA) = data;
                        //     RIA_RW1 = xram[RIA_ADDR1];
                        //     break;
                        // case CASE_WRIT(0xFFE8): // W XRAM1
                        //     xram[RIA_ADDR1] = data;
                        //     PIX_SEND_XRAM(RIA_ADDR1, data);
                        //     RIA_RW0 = xram[RIA_ADDR0];
                        //     __attribute__((fallthrough));
                        // case CASE_READ(0xFFE8): // R XRAM1
                        //     RIA_ADDR1 += RIA_STEP1;
                        //     RIA_RW1 = xram[RIA_ADDR1];
                        //     break;
                        // case CASE_WRIT(0xFFE7): // Set XRAM >ADDR0
                        //     REGS(0xFFE7) = data;
                        //     RIA_RW0 = xram[RIA_ADDR0];
                        //     break;
                        // case CASE_WRIT(0xFFE6): // Set XRAM <ADDR0
                        //     REGS(0xFFE6) = data;
                        //     RIA_RW0 = xram[RIA_ADDR0];
                        //     break;
                        // case CASE_WRIT(0xFFE4): // W XRAM0
                        //     xram[RIA_ADDR0] = data;
                        //     PIX_SEND_XRAM(RIA_ADDR0, data);
                        //     RIA_RW1 = xram[RIA_ADDR1];
                        //     __attribute__((fallthrough));
                        // case CASE_READ(0xFFE4): // R XRAM0
                        //     RIA_ADDR0 += RIA_STEP0;
                        //     RIA_RW0 = xram[RIA_ADDR0];
                        //     break;
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
                    }
                    // ------ FFD0 - FFDF ------ (DMA, FS)
                    else if (addr >= 0xFFD0)
                    {
                    }
                    // ------ FFC0 - FFCF ------ (MUL/DIV, TOD)
                    else if (addr >= 0xFFC0)
                    {
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
