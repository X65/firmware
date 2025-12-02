/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/pix.h"
#include "api/api.h"
#include "hw.h"
#include "main.h"
#include "pix.pio.h"
#include "sys/vpu.h"
#include <hardware/clocks.h>
#include <hardware/pio.h>
#include <pico/critical_section.h>
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

static volatile int pix_in_flight = 0;
static volatile pix_response_t *pix_response = nullptr;
static volatile int pix_response_skip = 0;
static critical_section_t pix_cs;
static absolute_time_t pix_last_activity;

#define PIX_PING_TIMEOUT_US (67 * 2)
#define PIX_ACK_TIMEOUT_MS  50

// Send a single PIX byte, block if necessary. Normally, blocking is bad, but
// this unblocks so fast that it's not a problem for a few messages.
static inline __attribute__((always_inline)) void pix_send_blocking(uint8_t byte)
{
    while (PIX_PIO->fstat & (1u << (PIO_FSTAT_TXFULL_LSB + PIX_SM)))
        tight_loop_contents();
    PIX_PIO->txf[PIX_SM] = byte;
}

static void __isr pix_irq_handler(void)
{
    PIX_PIO->irq = (1u << PIX_INT_NUM); // pio_interrupt_clear(PIX_PIO, PIX_INT_NUM);

    const uint16_t reply = PIX_PIO->rxf[PIX_SM];
    const uint8_t code = PIX_REPLY_CODE(reply);
    // printf("!!! %2X: %03X\n", code, PIX_REPLY_PAYLOAD(reply));

    if (pix_in_flight)
        --pix_in_flight;
    else
    {
        printf("PIX Unexpected Reply with no requests: %04X\n", reply);
        main_stop();
    }

    switch (code)
    {
    case PIX_ACK:
        vpu_raster = PIX_REPLY_PAYLOAD(reply);
        break;
    case PIX_DEV_DATA:
        if (!pix_response)
        {
            printf("PIX Unexpected PIX_DEV_DATA: %04X\n", reply);
            main_stop();
        }
        break;
    case PIX_NAK:
        vpu_raster = PIX_REPLY_PAYLOAD(reply);
        [[fallthrough]];
    default:
        printf("<<< %2X: %03X\n", code, PIX_REPLY_PAYLOAD(reply));
    }

    if (pix_response && pix_response_skip-- == 0)
    {
        pix_response->reply = reply;
        pix_response->status = 1;
        pix_response = nullptr;
    }
}

void pix_send_request(pix_req_type_t msg_type,
                      uint8_t req_len5, uint8_t *req_data,
                      pix_response_t *resp)
{
    assert(req_len5 > 0);
    assert(req_data);

    critical_section_enter_blocking(&pix_cs);

    if (pix_response)
    {
        // Previous request still pending
        printf("PIX Request Busy\n");
        while (pix_response)
            tight_loop_contents();
    }

    pix_last_activity = get_absolute_time();
    pix_response = resp;
    pix_response_skip = pix_in_flight++;
    pix_send_blocking(PIX_MESSAGE(msg_type, req_len5));

    if (req_len5 == 1)
    {
        pix_send_blocking(*req_data);
    }
    else
    {
        // FIXME: this should be done by DMA hardware
        while (req_len5--)
        {
            pix_send_blocking(*req_data++);
        }
    }

    critical_section_exit(&pix_cs);
}

void pix_init(void)
{
    critical_section_init(&pix_cs);

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

    pio_interrupt_clear(PIX_PIO, PIX_INT_NUM);
    // Route PIO internal IRQ to PIO_IRQ_
    pio_set_irq0_source_enabled(PIX_PIO, pis_interrupt0, true);
    irq_set_exclusive_handler(PIO_IRQ_NUM(PIX_PIO, 0), pix_irq_handler);
    irq_set_enabled(PIO_IRQ_NUM(PIX_PIO, 0), true);

    pio_sm_set_enabled(PIX_PIO, PIX_SM, true);
}

void pix_task(void)
{
    // If nothing happens, retrieve ACK with raster line.
    if (pio_sm_is_tx_fifo_empty(PIX_PIO, PIX_SM)
        && pix_in_flight == 0
        && absolute_time_diff_us(pix_last_activity, get_absolute_time()) > PIX_PING_TIMEOUT_US)
    {
        pix_send_request(PIX_SYNC, 1, (uint8_t[]) {0}, nullptr);
    }

    // Check whether PIX is running at all.
    if (pix_in_flight > 0
        && absolute_time_diff_us(pix_last_activity, get_absolute_time()) > PIX_ACK_TIMEOUT_MS * 1000)
    {
        printf("PIX FAILED\n");
        main_stop();
    }
}

void pix_stop(void)
{
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

inline void __attribute__((always_inline)) __attribute__((optimize("O3")))
pix_mem_write(uint32_t addr24, uint8_t data)
{
    pix_send_request(PIX_MEM_WRITE, 4,
                     (uint8_t[]) {(uint8_t)(addr24 >> 16),
                                  (uint8_t)(addr24 >> 8),
                                  (uint8_t)(addr24 & 0xFF),
                                  data},
                     nullptr);
}
