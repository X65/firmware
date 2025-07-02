/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/cpu.h"
#include "hardware/gpio.h"
#include "main.h"
#include "pico/stdio.h"
#include "pico/time.h"
#include "sys/cfg.h"
#include "sys/com.h"
#include "sys/mem.h"

static bool cpu_run_requested;
static absolute_time_t cpu_resb_timer;

volatile int cpu_rx_char;
static size_t cpu_rx_tail;
static size_t cpu_rx_head;
static uint8_t cpu_rx_buf[32];
#define CPU_RX_BUF(pos) cpu_rx_buf[(pos) & 0x1F]

void cpu_init(void)
{
    // drive reset pin
    gpio_init(CPU_RESB_PIN);
    gpio_put(CPU_RESB_PIN, false);
    gpio_set_dir(CPU_RESB_PIN, true);
}

void cpu_post_reclock(void)
{
    if (!gpio_get(CPU_RESB_PIN))
        cpu_resb_timer = delayed_by_us(get_absolute_time(), cpu_get_reset_us());
}

static uint8_t cpu_caps(uint8_t ch)
{
    switch (cfg_get_caps())
    {
    case 1:
        if (ch >= 'A' && ch <= 'Z')
        {
            ch += 32;
            break;
        }
        __attribute__((fallthrough));
    case 2:
        if (ch >= 'a' && ch <= 'z')
            ch -= 32;
    }
    return ch;
}

static int cpu_getchar_fifo(void)
{
    if (&CPU_RX_BUF(cpu_rx_head) != &CPU_RX_BUF(cpu_rx_tail))
        return CPU_RX_BUF(++cpu_rx_tail);
    return -1;
}

void cpu_task(void)
{
    // Enforce minimum RESB time
    if (cpu_run_requested && !gpio_get(CPU_RESB_PIN))
    {
        absolute_time_t now = get_absolute_time();
        if (absolute_time_diff_us(now, cpu_resb_timer) < 0)
            gpio_put(CPU_RESB_PIN, true);
    }

    // Move UART FIFO into action loop
    if (cpu_rx_char < 0)
    {
        cpu_rx_char = cpu_getchar_fifo();
        if (cpu_rx_char >= 0)
            cpu_rx_char = cpu_caps((uint8_t)cpu_rx_char);
    }
}

static void clear_com_rx_fifo(void)
{
    cpu_rx_char = -1;
    cpu_rx_tail = cpu_rx_head = 0;
}

void cpu_run(void)
{
    cpu_run_requested = true;
    clear_com_rx_fifo();
}

void cpu_stop(void)
{
    clear_com_rx_fifo();

    cpu_run_requested = false;
    gpio_put(CPU_RESB_PIN, false);
    cpu_resb_timer = delayed_by_us(get_absolute_time(),
                                   cpu_get_reset_us());
}

bool cpu_active(void)
{
    return cpu_run_requested;
}

uint32_t cpu_get_reset_us(void)
{
#ifndef RP6502_RESB_US
#define RP6502_RESB_US 0
#endif
    // If provided, use RP6502_RESB_US unless PHI2 speed needs
    // longer for 8 clock cycles (7 required, 1 for safety).
    uint32_t reset_us = 1;
    if (!RP6502_RESB_US)
        return reset_us;
    return RP6502_RESB_US < reset_us
               ? reset_us
               : RP6502_RESB_US;
}

void cpu_com_rx(uint8_t ch)
{
    // discarding overflow
    if (&CPU_RX_BUF(cpu_rx_head + 1) != &CPU_RX_BUF(cpu_rx_tail))
        CPU_RX_BUF(++cpu_rx_head) = ch;
}

// Used by std.c to get stdin destined for the CPU.
// Mixing RIA register input with read() calls isn't perfect,
// should be considered underfined behavior, and is discouraged.
// Even with a mutex, nulls may appear from RIA register.
uint8_t cpu_getchar(void)
{
    // Steal char from RIA register
    if (REGS(0xFFE0) & 0b01000000)
    {
        REGS(0xFFE0) &= ~0b01000000;
        uint8_t ch = REGS(0xFFE2);
        // Replace char with null
        REGS(0xFFE2) = 0;
        return cpu_caps(ch);
    }
    // Steal char from action loop queue
    if (cpu_rx_char >= 0)
    {
        uint8_t ch = (uint8_t)cpu_rx_char;
        cpu_rx_char = -1;
        return cpu_caps(ch);
    }
    // Get char from FIFO
    int ch = cpu_getchar_fifo();
    // Get char from UART
    if (ch < 0)
        ch = getchar_timeout_us(0);
    return cpu_caps((uint8_t)ch);
}
