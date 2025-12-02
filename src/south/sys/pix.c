/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/pix.h"
#include "cgia/cgia.h"
#include "hw.h"
#include "pix.pio.h"
#include "sys/out.h"
#include "sys/sys.h"
#include "term/font.h"
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/pio.h>
#include <pico/stdlib.h>
#include <stdio.h>
#include <string.h>

volatile const uint8_t xram[0x10000] __attribute__((aligned(0x10000)));

static uint8_t __attribute__((aligned(4))) pix_buffer[32];

static int pix_req_dma_chan;

static inline void __attribute__((always_inline))
pix_ack(void)
{
    *(io_rw_16 *)&PIX_PIO->txf[PIX_SM]
        = PIX_RESPONSE(PIX_ACK, cgia_reg_read(CGIA_REG_RASTER));
}

static inline void __attribute__((always_inline))
pix_nak(void)
{
    *(io_rw_16 *)&PIX_PIO->txf[PIX_SM]
        = PIX_RESPONSE(PIX_NAK, cgia_reg_read(CGIA_REG_RASTER));
}

static void __isr pix_irq_handler(void)
{

    PIX_PIO->irq = (1u << PIX_INT_NUM); // pio_interrupt_clear(PIX_PIO, PIX_INT_NUM);

    const uint8_t header = PIX_PIO->rxf[PIX_SM];
    const uint8_t frame_count = (header & 0b11111) + 1;

    dma_channel_set_transfer_count(pix_req_dma_chan, frame_count, false);
    if (frame_count < 5)
    {
        // normal command - drain FIFO to buffer
        dma_channel_set_write_addr(pix_req_dma_chan, pix_buffer, true);
        dma_channel_wait_for_finish_blocking(pix_req_dma_chan);
    }

    switch (header >> 5)
    {
    case PIX_SYNC:
        pix_ack();
        break;
    case PIX_MEM_WRITE:
    {
        if (frame_count != 4)
            goto unknown;
        // printf("PIX_MEM_WRITE %06lX %02X\n",
        //        pix_buffer[0] << 16 | pix_buffer[1] << 8 | pix_buffer[2], pix_buffer[3]);
        cgia_ram_write(pix_buffer[0],
                       (uint16_t)(pix_buffer[1] << 8 | pix_buffer[2]),
                       pix_buffer[3]);
        pix_ack();
    }
    break;
    case PIX_DEV_CMD:
    {
        const uint8_t dev_cmd = pix_buffer[0];
        const uint8_t device = (dev_cmd >> 4) & 0x0F;
        const uint8_t cmd = dev_cmd & 0x0F;
        // printf("PIX_DEV_CMD %02X %02X %02X\n", dev_cmd, device, cmd);

        switch (device)
        {
        case PIX_DEV_VPU:
            switch (cmd)
            {
            case PIX_VPU_CMD_GET_VERSION:
            {
                char ver_string[VPU_VERSION_MESSAGE_SIZE];
                memset(ver_string, 0, VPU_VERSION_MESSAGE_SIZE);
                strcpy(ver_string, sys_version());
                uart_write_blocking(COM_UART_INTERFACE,
                                    (const uint8_t *)ver_string, VPU_VERSION_MESSAGE_SIZE);
                pix_ack();
            }
            break;
            case PIX_VPU_CMD_GET_STATUS:
                sys_write_status();
                pix_ack();
                break;
            case PIX_VPU_CMD_SET_MODE_VT:
                out_set_mode(OUT_MODE_VT);
                pix_ack();
                break;
            case PIX_VPU_CMD_SET_MODE_CGIA:
                out_set_mode(OUT_MODE_CGIA);
                pix_ack();
                break;
            case PIX_VPU_CMD_SET_CODE_PAGE:
                font_set_code_page((uint16_t)(pix_buffer[1] << 8 | pix_buffer[2]));
                pix_ack();
                break;
            }
            break;
        }
    }
    break;
    case PIX_DEV_READ:
    {
        const uint8_t device = pix_buffer[0];
        switch (device)
        {
        case PIX_DEV_VPU:
        {
            const uint8_t reg = pix_buffer[1];
            const uint8_t value = cgia_reg_read(reg);
            // printf("PIX_DEV_READ VPU REG %02X = %02X\n", reg, value);
            *(io_rw_16 *)&PIX_PIO->txf[PIX_SM]
                = PIX_RESPONSE(PIX_DEV_DATA, value);
        }
        break;
        }
    }
    break;
    case PIX_DEV_WRITE:
    {
        const uint8_t device = pix_buffer[0];
        switch (device)
        {
        case PIX_DEV_VPU:
        {
            const uint8_t reg = pix_buffer[1];
            const uint8_t value = pix_buffer[2];
            // printf("%02X=%02X\n", reg, value);
            cgia_reg_write(reg, value);
            pix_ack();
        }
        break;
        }
    }
    break;
    default:
    unknown:
    {
        printf("PIX Unknown MSG: %02X/%d\n", header, frame_count);
        dma_channel_wait_for_finish_blocking(pix_req_dma_chan);
        printf(">>> %02X: ", header);
        for (int i = 0; i < frame_count; i++)
        {
            printf("%02X ", pix_buffer[i]);
        }
        printf("\n");

        pix_nak();
    }
    break;
    }
}

static void __isr pix_dma_handler(void)
{
    // Clear the interrupt request.
    dma_hw->ints0 = 1u << pix_req_dma_chan;
    printf("PIX DMA Done\n");
}

void pix_init(void)
{
    pio_set_gpio_base(PIX_PIO, 16);
    uint offset = pio_add_program(PIX_PIO, &pix_sb_program);
    pio_sm_config config = pix_sb_program_get_default_config(offset);
    const float clkdiv = (float)(clock_get_hz(clk_sys)) / (PIX_BUS_PIO_SPEED_KHZ * KHZ);
    sm_config_set_clkdiv(&config, clkdiv);
    sm_config_set_out_pin_base(&config, PIX_PIN_BASE);
    sm_config_set_out_pin_count(&config, 8);
    sm_config_set_in_pin_base(&config, PIX_PIN_BASE);
    sm_config_set_in_pin_count(&config, 8);
    sm_config_set_sideset_pin_base(&config, PIX_PIN_DTR);
    sm_config_set_jmp_pin(&config, PIX_PIN_SCK);
    sm_config_set_out_shift(&config, false, false, 0);
    sm_config_set_in_shift(&config, false, true, 8);
    for (int i = 0; i < PIX_PINS_USED; i++)
        pio_gpio_init(PIX_PIO, PIX_PIN_BASE + i);
    pio_sm_init(PIX_PIO, PIX_SM, offset, &config);
    pio_sm_set_consecutive_pindirs(PIX_PIO, PIX_SM, PIX_PIN_BASE, PIX_PINS_USED, false);
    pio_sm_set_consecutive_pindirs(PIX_PIO, PIX_SM, PIX_PIN_DTR, 1, true);

    pio_interrupt_clear(PIX_PIO, PIX_INT_NUM);
    // Route PIO internal IRQ to PIO_IRQ_
    pio_set_irq0_source_enabled(PIX_PIO, pis_interrupt0, true);
    irq_set_exclusive_handler(PIO_IRQ_NUM(PIX_PIO, 0), pix_irq_handler);
    irq_set_enabled(PIO_IRQ_NUM(PIX_PIO, 0), true);

    // DMA for bulk transfers
    pix_req_dma_chan = dma_claim_unused_channel(true);
    dma_channel_config dma_config = dma_channel_get_default_config(pix_req_dma_chan);
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_8);
    channel_config_set_read_increment(&dma_config, false);
    channel_config_set_write_increment(&dma_config, true);
    channel_config_set_dreq(&dma_config, PIO_DREQ_NUM(PIX_PIO, PIX_SM, false));
    dma_channel_configure(
        pix_req_dma_chan,
        &dma_config,
        pix_buffer,
        &PIX_PIO->rxf[PIX_SM],
        0,
        false);
    dma_channel_set_irq0_enabled(pix_req_dma_chan, true);
    irq_set_exclusive_handler(PIX_DMA_IRQ, pix_dma_handler);
    irq_set_enabled(PIX_DMA_IRQ, true);

    pio_sm_set_enabled(PIX_PIO, PIX_SM, true);
}

void pix_task(void)
{
    // uint8_t ch = (raw & 0x0F000000) >> 24;
    // uint8_t addr = (raw & 0x00FF0000) >> 16;
    // uint16_t word = raw & 0xFFFF;
    // // These return true on slow operations to
    // // allow us to stay greedy on fast ones.
    // if (ch == 0 && pix_ch0_xreg(addr, word))
    //     break;
    // if (ch == 15 && pix_ch15_xreg(addr, word))
    //     break;
}
