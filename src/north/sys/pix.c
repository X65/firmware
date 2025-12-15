/*
 * Copyright (c) 2025 Tomasz Sterna
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
#include <pico/mutex.h>
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
static critical_section_t pix_resp_cs;
static mutex_t pix_send_mutex;
static absolute_time_t pix_last_activity;

static uint16_t pix_dma_blocks_remaining = 0;
static uint8_t pix_dma_bank = 0;
static uint16_t pix_dma_offset = 0;

#define PIX_ACK_TIMEOUT_MS 50

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

    critical_section_enter_blocking(&pix_resp_cs);
    --pix_in_flight;
    if (pix_in_flight < 0)
    {
        printf("PIX Unexpected Reply with no requests: %04X\n", reply);
        main_stop();
        pix_in_flight = 0;
    }
    critical_section_exit(&pix_resp_cs);

    pix_response_t *pix_resp = pix_response;
    if (pix_response && pix_response_skip-- == 0)
    {
        pix_response = nullptr;
        pix_resp->reply = reply;
        pix_resp->status = 1;
    }

    switch (code)
    {
    case PIX_PONG:
        break;
    case PIX_ACK:
        vpu_set_raster(PIX_REPLY_PAYLOAD(reply));
        break;
    case PIX_DMA_REQ:
        pix_dma_bank = (uint8_t)PIX_REPLY_PAYLOAD(reply);
        pix_dma_offset = 0;
        pix_dma_blocks_remaining = 0x10000 / 32; // 64kB in 32-byte blocks
        break;
    case PIX_DEV_DATA:
        if (!pix_resp)
        {
            printf("PIX Unexpected PIX_DEV_DATA: %04X\n", reply);
            main_stop();
        }
        break;
    case PIX_NAK:
        vpu_set_raster(PIX_REPLY_PAYLOAD(reply));
        [[fallthrough]];
    default:
        printf("<<< %2X: %03X\n", code, PIX_REPLY_PAYLOAD(reply));
    }
}

void pix_send_request(pix_req_type_t msg_type,
                      uint8_t req_len5, uint8_t *req_data,
                      pix_response_t *resp)
{
    assert(req_len5 > 0);
    assert(req_data);
    assert(!resp || resp->status == 0);

    mutex_enter_blocking(&pix_send_mutex);

    // spin wait if request with response is pending
    while (pix_response)
        tight_loop_contents();
    // while (dma_is_running) tight_loop_contents();

    critical_section_enter_blocking(&pix_resp_cs);
    pix_response = resp;
    pix_response_skip = pix_in_flight++;
    critical_section_exit(&pix_resp_cs);

    pix_last_activity = get_absolute_time();

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

    mutex_exit(&pix_send_mutex);
}

void pix_init(void)
{
    critical_section_init(&pix_resp_cs);
    mutex_init(&pix_send_mutex);

    pio_set_gpio_base(PIX_PIO, 16);
    uint offset = pio_add_program(PIX_PIO, &pix_nb_program);
    pio_sm_config config = pix_nb_program_get_default_config(offset);
    const float clkdiv = (float)(clock_get_hz(clk_sys)) / (PIX_BUS_PIO_SPEED_KHZ * KHZ);
    sm_config_set_clkdiv(&config, clkdiv);
    sm_config_set_out_pin_base(&config, PIX_PIN_BASE);
    sm_config_set_out_pin_count(&config, 8);
    sm_config_set_in_pin_base(&config, PIX_PIN_BASE);
    sm_config_set_in_pin_count(&config, PIX_PINS_USED);
    sm_config_set_sideset_pin_base(&config, PIX_PIN_RTS);
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
#if 0
    {
        printf("PIX testing...\n");

        uint8_t seq = 0;
        uint8_t payload[32] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                               17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
        pix_response_t resp = {0};
        while (true)
        {
            const uint8_t len5 = (seq & 0x1F) + 1;
            pix_send_request(PIX_PING, len5, &payload[0], &resp);
            while (!resp.status)
                tight_loop_contents();
            if (PIX_REPLY_CODE(resp.reply) != PIX_PONG
                || ((payload[seq & 0x1F] << 6) | len5) != PIX_REPLY_PAYLOAD(resp.reply))
            {
                printf("!!! %02X => %X\n", seq, resp.reply);
                while (true)
                    tight_loop_contents();
            }
            pix_send_request(PIX_PING, (len5 + 5) % 32 + 1, &payload[0], nullptr);
            ++seq;
            if (seq == 0)
            {
                putchar('.');
            }
        }
    }
#endif

    // If nothing happens, push DMA or retrieve ACK with raster line.
    if (pio_sm_is_tx_fifo_empty(PIX_PIO, PIX_SM)
        && pix_in_flight == 0)
    {
        if (pix_dma_blocks_remaining > 0)
        {
            pix_send_request(PIX_DMA_WRITE, 32, mem_fetch_row(pix_dma_bank, pix_dma_offset), nullptr);
            pix_dma_offset += 32;
            --pix_dma_blocks_remaining;
        }
        else
        {
            pix_send_request(PIX_SYNC, 1, (uint8_t[]) {0}, nullptr);
        }
    }

    // Check whether PIX is running at all.
    if (pix_in_flight > 0
        && absolute_time_diff_us(pix_last_activity, get_absolute_time()) > PIX_ACK_TIMEOUT_MS * 1000)
    {
        printf("PIX FAILED\n");
        // while (true)
        //     tight_loop_contents();
        main_stop();
    }
}

void pix_stop(void)
{
}

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
