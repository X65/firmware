/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "./hst.h"
#include "hw.h"
#include "sys/sgu.h"

#include <hardware/gpio.h>
#include <hardware/spi.h>

#define SPI_READ_BIT 0x8000

#define SPI_IRQ_NUM(spi) (((spi) == spi0) ? SPI0_IRQ : SPI1_IRQ)

static void __isr __not_in_flash_func(hst_spi_irq_handler)(void)
{
    spi_hw_t *spi_hw = spi_get_hw(HST_SPI);

    while (spi_is_readable(HST_SPI))
    {
        const uint16_t rcv = (uint16_t)spi_hw->dr;
        const uint8_t reg = (rcv >> 8) & (SGU_REGS_PER_CH - 1);

        if (rcv & SPI_READ_BIT)
        {
            // read command - enqueue answer
            spi_hw->dr = (rcv & 0xFF00) | (uint16_t)sgu_reg_read(reg);
        }
        else
        {
            // write command
            sgu_reg_write(reg, (uint8_t)(rcv & 0xFF));
        }
    }
}

void hst_init(void)
{
    // Configure SPI communication
    spi_init(HST_SPI, HST_BAUDRATE_HZ);
    spi_set_slave(HST_SPI, true);
    spi_set_format(HST_SPI, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(HST_SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(HST_SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(HST_SPI_TX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(HST_SPI_CS_PIN, GPIO_FUNC_SPI);

    // SPI IRQ on RX FIFO
    spi_get_hw(HST_SPI)->imsc = SPI_SSPIMSC_RXIM_BITS;
    irq_set_exclusive_handler(SPI_IRQ_NUM(HST_SPI), hst_spi_irq_handler);
    irq_set_enabled(SPI_IRQ_NUM(HST_SPI), true);
}

void hst_task(void)
{
}
