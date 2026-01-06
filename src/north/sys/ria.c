/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/ria.h"
#include "api/api.h"
#include "hw.h"
#include "main.h"
#include "south/cgia/cgia.h"
#include "sys/cia.h"
#include "sys/com.h"
#include "sys/mem.h"
#include "sys/pix.h"
#include "sys/vpu.h"
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/pio.h>
#include <littlefs/lfs_util.h>
#include <pico/critical_section.h>
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

static uint8_t RGB_regs[8] = {0};
static uint8_t BUZ_regs[4] = {0};

static volatile uint8_t irq_mask = 0;
static volatile uint8_t irq_state = 0;
static critical_section_t irq_lock;

static inline __force_inline void ria_update_irq_pin(void)
{
    gpio_put(RIA_IRQB_PIN, !(irq_state & irq_mask));
}

void ria_set_irq(uint8_t source)
{
    critical_section_enter_blocking(&irq_lock);
    irq_state |= source;
    ria_update_irq_pin();
    critical_section_exit(&irq_lock);
}

void ria_clear_irq(uint8_t source)
{
    critical_section_enter_blocking(&irq_lock);
    irq_state &= ~source;
    ria_update_irq_pin();
    critical_section_exit(&irq_lock);
}

#define MEM_LOG_ENABLED (0)
static bool mem_dump = false;
static int mem_log_idx = 0;
#if MEM_LOG_ENABLED
#define MEM_LOG_SIZE 360
static uint32_t mem_addr[MEM_LOG_SIZE];
static uint8_t mem_data[MEM_LOG_SIZE];
#endif

#define CPU_VAB_MASK    (1u << 24)
#define CPU_RWB_MASK    (1u << 25)
#define CPU_IODEV_MASK  0xFF
#define CASE_READ(addr) (CPU_RWB_MASK | ((addr & CPU_IODEV_MASK) << 8))
#define CASE_WRIT(addr) ((addr & CPU_IODEV_MASK) << 8)

static void ria_mem_dump(void)
{
#if MEM_LOG_ENABLED
    for (int i = 0; i < mem_log_idx; i++)
    {
        printf("%02d: %02X %02X%04X %c %02X\n",
               i, mem_addr[i] >> 24, (uint8_t)(mem_addr[i]), (uint16_t)(mem_addr[i] >> 8),
               (mem_addr[i] & CPU_RWB_MASK) ? 'R' : 'W', mem_data[i]);
    }
#endif
}

void ria_run(void)
{
    mem_log_idx = 0;
    mem_dump = false;
    ria_update_irq_pin();
}

void ria_stop(void)
{
    irq_state = irq_mask = 0x00;
    ria_update_irq_pin();

    if (mem_dump)
        ria_mem_dump();
}

void ria_task(void)
{
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

            const bool is_read = (rw_addr_bus & CPU_RWB_MASK);

            if (!is_read)
            {
                // CPU is writing - wait and pull from data bus
                while ((CPU_BUS_PIO->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + CPU_BUS_SM))))
                    tight_loop_contents();
                data = (uint8_t)CPU_BUS_PIO->rxf[CPU_BUS_SM];
            }

            // I/O devices mmapped here
            if ((rw_addr_bus & 0xFC00FF) == 0xFC0000)
            {
                const uint16_t addr = (uint16_t)(rw_addr_bus >> 8);

                // RIA registers
                if (addr >= 0xFFC0)
                {
                    // ------ FFF0 - FFFF ------ (API, EXT CTL)
                    if (addr >= 0xFFF0)
                        switch (rw_addr_bus & (CPU_RWB_MASK | (CPU_IODEV_MASK << 8)))
                        {
                        case CASE_READ(0xFFF3): // API STATUS
                            data = API_STATUS;
                            break;
                        case CASE_WRIT(0xFFF2): // xstack
                            if (xstack_ptr)
                                xstack[--xstack_ptr] = data;
                            break;
                        case CASE_READ(0xFFF2): // xstack
                            data = xstack[xstack_ptr];
                            if (xstack_ptr < XSTACK_SIZE)
                                ++xstack_ptr;
                            break;
                        case CASE_WRIT(0xFFF0): // RIA API function call
                            api_set_regs_blocked();
                            if (data == API_OP_ZXSTACK)
                            {
                                xstack_ptr = XSTACK_SIZE;
                                api_return_ax(0);
                            }
                            else if (data == API_OP_HALT)
                            {
                                gpio_put(CPU_RESB_PIN, false);
                                mem_dump = true;
                                main_stop();
                            }
                            else
                            {
                                API_OP = data;
                            }
                            break;
                        default:
                        {
                            if (is_read)
                                data = regs[addr & 0x3F];
                            else
                                regs[addr & 0x3F] = data;
                        }
                        }
                    // ------ FFE0 - FFEF ------ (UART, RNG, IRQ CTL)
                    else if (addr >= 0xFFE0)
                        switch (rw_addr_bus & (CPU_RWB_MASK | (CPU_IODEV_MASK << 8)))
                        {
                        case CASE_READ(0xFFED): // IRQ_STATUS
                            data = 0xFF;
                            break;
                        case CASE_WRIT(0xFFEC): // IRQ Mask
                            irq_mask = data;
                            ria_update_irq_pin();
                            break;
                        case CASE_READ(0xFFEC):
                            data = irq_mask;
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
                            // printf("R %02X %c\n", ch, (ch >= 0) ? ' ' : 'x');
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
                            // printf("W %02X (%c) %c\n", data, isprint(data) ? data : '_', com_tx_writable() ? 'v' : 'x');
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
                            // printf("! %02X\n", status);
                            break;
                        }
                        default:
                        {
                            if (is_read)
                                data = regs[addr & 0x3F];
                            else
                                regs[addr & 0x3F] = data;
                        }
                        }

                    // ------ FFD0 - FFDF ------ (DMA, FS)
                    else if (addr >= 0xFFD0)
                        switch (rw_addr_bus & (CPU_RWB_MASK | (CPU_IODEV_MASK << 8)))
                        {
                        default:
                        {
                            if (is_read)
                                data = regs[addr & 0x3F];
                            else
                                regs[addr & 0x3F] = data;
                        }
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
                        default:
                        {
                            if (is_read)
                                data = regs[addr & 0x3F];
                            else
                                regs[addr & 0x3F] = data;
                        }
                        }
                }
                // devices Memory-MAPped by RIA
                else if (addr >= 0xFF80)
                {
                    // ------ FFAC - FFBF ------
                    if (addr >= 0xFFAC)
                    {
                        data = 0xFF; // unused
                    }
                    // ------ FFA8 - FFAB ------ (BUZZer)
                    else if (addr >= 0xFFA8)
                    {
                        const uint8_t reg = addr & 0x03;
                        if (is_read)
                            data = BUZ_regs[reg];
                        else
                        {
                            BUZ_regs[reg] = data;

                            switch (reg)
                            {
                            case 0:
                            case 1:
                                pix_send_request(PIX_DEV_CMD, 3,
                                                 (uint8_t[]) {
                                                     PIX_DEVICE_CMD(PIX_DEV_MISC, PIX_BUZ_CMD_SET_FREQ),
                                                     BUZ_regs[0],
                                                     BUZ_regs[1],
                                                 },
                                                 nullptr);
                                break;
                            case 2:
                                pix_send_request(PIX_DEV_CMD, 2,
                                                 (uint8_t[]) {
                                                     PIX_DEVICE_CMD(PIX_DEV_MISC, PIX_BUZ_CMD_SET_DUTY),
                                                     data,
                                                 },
                                                 nullptr);
                                break;
                            }
                        }
                    }
                    // ------ FFA0 - FFA7 ------ (RGB LEDs - WS2812B strip)
                    else if (addr >= 0xFFA0)
                    {
                        const uint8_t reg = addr & 0x07;
                        if (is_read)
                            data = RGB_regs[reg];
                        else
                        {
                            RGB_regs[reg] = data;

                            switch (reg)
                            {
                            case 0:
                            case 1:
                            case 2:
                            case 3: // RGB332 LED set
                                pix_send_request(PIX_DEV_CMD, 3,
                                                 (uint8_t[]) {
                                                     PIX_DEVICE_CMD(PIX_DEV_MISC, PIX_LED_CMD_SET_RGB332),
                                                     reg,
                                                     data,
                                                 },
                                                 nullptr);
                                break;
                            case 4: // RGB888 LED set
                                pix_send_request(PIX_DEV_CMD, 5,
                                                 (uint8_t[]) {
                                                     PIX_DEVICE_CMD(PIX_DEV_MISC, PIX_LED_CMD_SET_RGB888),
                                                     data,
                                                     RGB_regs[5],
                                                     RGB_regs[6],
                                                     RGB_regs[7],
                                                 },
                                                 nullptr);
                                break;
                            }
                        }
                    }
                    // ------ FF98 - FF9F ------ (CIA-compatible timers)
                    else if (addr >= 0xFF98)
                    {
                        const uint8_t reg = addr & 0x07;
                        if (is_read)
                        {
                            switch (reg)
                            {
                            case 0: // TIMER A low byte
                                data = cia_get_count(CIA_A) & 0xFF;
                                break;
                            case 1: // TIMER A high byte
                                data = cia_get_count(CIA_A) >> 8;
                                break;
                            case 2: // TIMER B low byte
                                data = cia_get_count(CIA_B) & 0xFF;
                                break;
                            case 3: // TIMER B high byte
                                data = cia_get_count(CIA_B) >> 8;
                                break;
                            case 5: // ICR
                                data = cia_get_icr();
                                break;
                            case 6: // CRA
                                data = cia_get_control(CIA_A);
                                break;
                            case 7: // CRB
                                data = cia_get_control(CIA_B);
                                break;
                            default:
                                data = 0xFF;
                            }
                        }
                        else
                        {
                            switch (reg)
                            {
                            case 0: // TIMER A low byte
                                cia_set_count_lo(CIA_A, data);
                                break;
                            case 1: // TIMER A high byte
                                cia_set_count_hi(CIA_A, data);
                                break;
                            case 2: // TIMER B low byte
                                cia_set_count_lo(CIA_B, data);
                                break;
                            case 3: // TIMER B high byte
                                cia_set_count_hi(CIA_B, data);
                                break;
                            case 5: // ICR
                                cia_set_icr(data);
                                break;
                            case 6: // CRA
                                cia_set_control(CIA_A, data);
                                break;
                            case 7: // CRB
                                cia_set_control(CIA_B, data);
                                break;
                            }
                        }
                    }
                    else
                    // ------ FF80 - FF97 ------ (GPIO extender)
                    {
                        data = 0xFF;
                    }
                }
                // CGIA ------ FF00 - FF7F ------
                else if (addr >= 0xFF00)
                {
                    const uint8_t reg = addr & 0x7F;
                    if (is_read)
                    {
                        // Short-circuit CGIA raster read to avoid req/resp latency.
                        // Current raster line is being sent with each PIX ACK/NACK response.
                        if ((reg & 0xFE) == CGIA_REG_RASTER && pix_raster_available())
                        {
                            data = (reg & 1) ? (uint8_t)(vpu_raster >> 8) : (uint8_t)(vpu_raster);
                        }
                        else
                        {
                            pix_response_t resp = {0};
                            pix_send_request(PIX_DEV_READ, 2,
                                             (uint8_t[]) {PIX_DEV_VPU, reg},
                                             &resp);
                            while (!resp.status)
                                tight_loop_contents();
                            data = PIX_REPLY_CODE(resp.reply) == PIX_DEV_DATA
                                       ? (uint8_t)PIX_REPLY_PAYLOAD(resp.reply)
                                       : 0xFF;
                        }
                    }
                    else
                    {
                        // printf("CGIA WR %02X=%02X\n", reg, data);
                        pix_send_request(PIX_DEV_WRITE, 3,
                                         (uint8_t[]) {PIX_DEV_VPU, reg, data},
                                         nullptr);
                    }
                }
                // SGU-1 ------ FEC0 - FEFF ------
                else if (addr >= 0xFEC0)
                {
                    const uint8_t reg = addr & 0x3F;
                    if (is_read)
                    {
                        pix_response_t resp = {0};
                        pix_send_request(PIX_DEV_READ, 2,
                                         (uint8_t[]) {PIX_DEV_SPU, reg},
                                         &resp);
                        while (!resp.status)
                            tight_loop_contents();
                        data = PIX_REPLY_CODE(resp.reply) == PIX_DEV_DATA
                                   ? (uint8_t)PIX_REPLY_PAYLOAD(resp.reply)
                                   : 0xFF;
                    }
                    else
                    {
                        // printf("SGU-1 WR %02X=%02X\n", reg, data);
                        pix_send_request(PIX_DEV_WRITE, 3,
                                         (uint8_t[]) {PIX_DEV_SPU, reg, data},
                                         nullptr);
                    }
                }

                // handled - move along
                goto act_loop_epilogue;
            }

            // else is "normal" memory access
            const uint32_t ram_addr = (((uint8_t)rw_addr_bus) << 16) | ((uint16_t)(rw_addr_bus >> 8));
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
#if MEM_LOG_ENABLED
            if (cpu_active())
            {
                if (mem_log_idx >= MEM_LOG_SIZE)
                    mem_log_idx = 0;
                mem_data[mem_log_idx] = data;
                mem_addr[mem_log_idx++] = rw_addr_bus;
            }
#endif
        }
    }
}

void ria_init(void)
{
    // drive irq pin
    gpio_init(RIA_IRQB_PIN);
    gpio_put(RIA_IRQB_PIN, true);
    gpio_set_dir(RIA_IRQB_PIN, true);

    critical_section_init(&irq_lock);

    multicore_launch_core1(act_loop);
}

void ria_print_status(void)
{
    const float clk = (float)(clock_get_hz(clk_sys));
    printf("CLKN: %.1fMHz\n", clk / MHZ);
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
