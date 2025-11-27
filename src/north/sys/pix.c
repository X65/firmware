/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/pix.h"
#include "../pix.h"
#include "api/api.h"
#include "hw.h"
#include "pix.pio.h"
#include <hardware/clocks.h>
#include <hardware/pio.h>
#include <pico/time.h>
#include <stdint.h>
#include <stdio.h>

#if defined(DEBUG_RIA_SYS) || defined(DEBUG_RIA_SYS_PIX)
#include <stdio.h>
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
static inline void DBG(const char *fmt, ...)
{
    (void)fmt;
}
#endif

static uint32_t pix_send_count;
static absolute_time_t pix_api_state_timer;
static enum state {
    pix_api_running,
    pix_api_waiting,
    pix_api_ack,
    pix_api_nak,
} pix_api_state;

#define PIX_ACK_TIMEOUT_MS 2

void pix_init(void)
{
    pio_set_gpio_base(PIX_PIO, 16);
    uint offset = pio_add_program(PIX_PIO, &pix_nb_program);
    pio_sm_config config = pix_nb_program_get_default_config(offset);
    const float clkdiv = (float)(clock_get_hz(clk_sys)) / (PIX_BUS_PIO_SPEED_KHZ * KHZ);
    sm_config_set_clkdiv(&config, clkdiv);
    sm_config_set_out_pin_base(&config, PIX_PIN_BASE);
    sm_config_set_out_pin_count(&config, 8);
    sm_config_set_in_pin_base(&config, PIX_PIN_BASE);
    sm_config_set_in_pin_count(&config, PIX_PINS_USED);
    sm_config_set_sideset_pin_base(&config, PIX_PIN_SCK);
    sm_config_set_jmp_pin(&config, PIX_PIN_DTR);
    sm_config_set_out_shift(&config, true, false, 0);
    sm_config_set_in_shift(&config, false, true, 16);
    for (int i = 0; i < PIX_PINS_USED; i++)
        pio_gpio_init(PIX_PIO, PIX_PIN_BASE + i);
    pio_sm_init(PIX_PIO, PIX_SM, offset, &config);
    pio_sm_set_consecutive_pindirs(PIX_PIO, PIX_SM, PIX_PIN_BASE, PIX_PINS_USED, true);
    pio_sm_set_consecutive_pindirs(PIX_PIO, PIX_SM, PIX_PIN_DTR, 1, false);
    // pio_sm_put(PIX_PIO, PIX_SM, PIX_MESSAGE(PIX_DEVICE_IDLE, 0, 0, 0));
    pio_sm_set_enabled(PIX_PIO, PIX_SM, true);

    // // Queue a couple sync frames for safety
    // pix_send(PIX_DEVICE_IDLE, 0, 0, 0);
    // pix_send(PIX_DEVICE_IDLE, 0, 0, 0);
}

static uint32_t sent = 0;
void pix_task(void)
{
    if (pio_sm_is_tx_fifo_empty(PIX_PIO, PIX_SM) && !(sent & 1))
    {
        const int msg_count = 31; // encoded as n-1
        pio_sm_put(PIX_PIO, PIX_SM, (0x1 << 5) | (msg_count & 0b11111));
        for (int i = 0; i <= msg_count; i++)
        {
            pio_sm_put_blocking(PIX_PIO, PIX_SM, 0x11 + i + sent);
        }
        ++sent;
    }
    while (!pio_sm_is_rx_fifo_empty(PIX_PIO, PIX_SM))
    {
        uint32_t raw = pio_sm_get(PIX_PIO, PIX_SM);
        printf("<<< %04lX\n", raw);
        if (sent < 6)
            ++sent;
    }
}

void pix_stop(void)
{
    pix_send_count = 0;
    pix_api_state = pix_api_running;
}

void pix_ack(void)
{
    if (pix_api_state == pix_api_waiting)
        pix_api_state = pix_api_ack;
}

void pix_nak(void)
{
    if (pix_api_state == pix_api_waiting)
        pix_api_state = pix_api_nak;
}

// bool pix_api_xreg(void)
// {
//     static uint8_t pix_device;
//     static uint8_t pix_channel;
//     static uint8_t pix_addr;

//     switch (pix_api_state)
//     {
//     case pix_api_running:
//         break;
//     case pix_api_waiting:
//         if (absolute_time_diff_us(get_absolute_time(), pix_api_state_timer) < 0)
//         {
//             pix_api_state = pix_api_running;
//             pix_send_count = 0;
//             return api_return_errno(API_EIO);
//         }
//         return api_working();
//     case pix_api_ack:
//         pix_api_state = pix_api_running;
//         if (pix_send_count == 0)
//         {
//             xstack_ptr = XSTACK_SIZE;
//             return api_return_ax(0);
//         }
//         break;
//     case pix_api_nak:
//         pix_api_state = pix_api_running;
//         pix_send_count = 0;
//         return api_return_errno(API_EINVAL);
//     }

//     // In progress, send one xreg
//     if (pix_send_count)
//     {
//         if (pix_ready())
//         {
//             --pix_send_count;
//             uint16_t data = 0;
//             api_pop_uint16(&data);
//             pix_send(pix_device, pix_channel, pix_addr + pix_send_count, data);
//             if (pix_device == PIX_DEVICE_VGA && pix_channel == 0 && pix_addr + pix_send_count <= 1)
//             {
//                 pix_api_state = pix_api_waiting;
//                 pix_api_state_timer = make_timeout_time_ms(PIX_ACK_TIMEOUT_MS);
//             }
//             else if (!pix_send_count)
//             {
//                 xstack_ptr = XSTACK_SIZE;
//                 return api_return_ax(0);
//             }
//         }
//         return api_working();
//     }

//     // Setup for new call
//     pix_device = xstack[XSTACK_SIZE - 1];
//     pix_channel = xstack[XSTACK_SIZE - 2];
//     pix_addr = xstack[XSTACK_SIZE - 3];
//     pix_send_count = (XSTACK_SIZE - xstack_ptr - 3) / 2;
//     if (!(xstack_ptr & 0x01)
//         || pix_send_count < 1 || pix_send_count > XSTACK_SIZE / 2
//         || pix_device > 7 || pix_channel > 15)
//     {
//         pix_send_count = 0;
//         return api_return_errno(API_EINVAL);
//     }

//     // Local PIX device $0
//     if (pix_device == PIX_DEVICE_RIA)
//     {
//         for (; pix_send_count; pix_send_count--)
//         {
//             uint16_t data = 0;
//             api_pop_uint16(&data);
//             if (!main_xreg(pix_channel, pix_addr, data))
//             {
//                 pix_send_count = 0;
//                 return api_return_errno(API_EINVAL);
//             }
//         }
//         xstack_ptr = XSTACK_SIZE;
//         return api_return_ax(0);
//     }

//     // Special case of sending VGA canvas and mode in same call.
//     // Because we send in reverse, canvas has to be first or it'll clear mode programming.
//     if (pix_device == PIX_DEVICE_VGA && pix_channel == 0 && pix_addr == 0 && pix_send_count > 1)
//     {
//         pix_send_blocking(PIX_DEVICE_VGA, 0, 0, *(uint16_t *)&xstack[XSTACK_SIZE - 5]);
//         pix_addr = 1;
//         pix_send_count -= 1;
//         pix_api_state = pix_api_waiting;
//         pix_api_state_timer = make_timeout_time_ms(PIX_ACK_TIMEOUT_MS);
//     }
//     return api_working();
// }
