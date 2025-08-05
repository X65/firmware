/*
 * Copyright (c) 2023 Rumbledethumps
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mem.h"
#include "hardware/gpio.h"
#include "hardware/regs/qmi.h"
#include "hardware/regs/xip.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/sync.h"
#include "littlefs/lfs_util.h"
#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

size_t psram_size[PSRAM_BANKS_NO];
uint8_t psram_readid_response[PSRAM_BANKS_NO][8];

// PSRAM SPI command codes
const uint8_t PSRAM_CMD_QUAD_END = 0xF5;
const uint8_t PSRAM_CMD_QUAD_ENABLE = 0x35;
const uint8_t PSRAM_CMD_READ_ID = 0x9F;
const uint8_t PSRAM_CMD_RSTEN = 0x66;
const uint8_t PSRAM_CMD_RST = 0x99;
const uint8_t PSRAM_CMD_WRAP_BOUNDARY = 0xC0;
const uint8_t PSRAM_CMD_QUAD_READ = 0xEB;
const uint8_t PSRAM_CMD_QUAD_WRITE = 0x38;
const uint8_t PSRAM_CMD_NOOP = 0xFF;

const uint8_t PSRAM_MF_AP = 0x0D;
const uint8_t PSRAM_KGD = 0b01011101;

// Activate PSRAM. (Copied from CircuitPython ports/raspberrypi/supervisor/port.c)
static void __no_inline_not_in_flash_func(setup_psram)(uint8_t bank)
{
    gpio_set_function(QMI_PSRAM_CS_PIN, GPIO_FUNC_XIP_CS1);
    psram_size[bank] = 0;
    uint32_t save_irq_status = save_and_disable_interrupts();
    // Try and read the PSRAM ID via direct_csr.
    qmi_hw->direct_csr = 30 << QMI_DIRECT_CSR_CLKDIV_LSB
                         | QMI_DIRECT_CSR_EN_BITS;
    // Need to poll for the cooldown on the last XIP transfer to expire
    // (via direct-mode BUSY flag) before it is safe to perform the first
    // direct-mode operation
    while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0)
    {
    }

    // Exit out of QMI in case we've inited already
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    // Transmit the command to exit QPI quad mode - read ID as standard SPI
    qmi_hw->direct_tx = QMI_DIRECT_TX_OE_BITS
                        | QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB
                        | PSRAM_CMD_QUAD_END;
    while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0)
    {
    }
    (void)qmi_hw->direct_rx;
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS);

    // Read the id
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    uint8_t kgd = 0;
    uint8_t eid = 0;
    for (size_t i = 0; i < 12; i++)
    {
        if (i == 0)
        {
            qmi_hw->direct_tx = PSRAM_CMD_READ_ID;
        }
        else
        {
            qmi_hw->direct_tx = PSRAM_CMD_NOOP;
        }
        while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_TXEMPTY_BITS) == 0)
        {
        }
        while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0)
        {
        }
        // buffer read id response eliding the first 4 bytes (cmd + 24-bit addr)
        if (i >= 4)
        {
            psram_readid_response[bank][i - 4] = qmi_hw->direct_rx;
        }
        else
        {
            (void)qmi_hw->direct_rx;
        }

        if (i == 5)
        {
            kgd = psram_readid_response[bank][i - 4];
        }
        else if (i == 6)
        {
            eid = psram_readid_response[bank][i - 4];
        }
    }
    // Disable direct csr.
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS | QMI_DIRECT_CSR_EN_BITS);

    if (kgd != PSRAM_KGD)
    {
        restore_interrupts(save_irq_status);
        return;
    }

    // Enable quad mode.
    qmi_hw->direct_csr = 30 << QMI_DIRECT_CSR_CLKDIV_LSB
                         | QMI_DIRECT_CSR_EN_BITS;
    // Need to poll for the cooldown on the last XIP transfer to expire
    // (via direct-mode BUSY flag) before it is safe to perform the first
    // direct-mode operation
    while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0)
    {
    }

    // RESETEN, RESET, quad enable, toggle wrap boundary mode
    for (uint8_t i = 0; i < 4; i++)
    {
        qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
        switch (i)
        {
        case 0:
            qmi_hw->direct_tx = PSRAM_CMD_RSTEN;
            break;
        case 1:
            qmi_hw->direct_tx = PSRAM_CMD_RST;
            break;
        case 2:
            qmi_hw->direct_tx = PSRAM_CMD_QUAD_ENABLE;
            break;
        case 3:
            qmi_hw->direct_tx = PSRAM_CMD_WRAP_BOUNDARY;
            break;
        }
        while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0)
        {
        }
        qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
        for (size_t j = 0; j < 20; j++)
        {
            asm("nop");
        }
        (void)qmi_hw->direct_rx;
    }
    // Disable direct csr.
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS | QMI_DIRECT_CSR_EN_BITS);

    // PSRAM timings with 150MHz SCK, 6.667 ns per cycle.
    //   COOLDOWN:     2'b01            64 SCK = 426.7ns
    //   PAGEBREAK:    2'b10            break bursts at 1024-byte page boundaries
    //   reserved:     2'b00
    //   SELECT_SETUP: 1'b0             0.5 SCK = 3.33ns
    //   SELECT_HOLD:  2'b11            1.0 SCK = 6.67ns
    //   MAX_SELECT:   6'b01_0000       16 x 64 SCK = 6.827us
    //   MIN_DESELECT: 4'b0111          7.5 x SCK = 50.0ns
    //   reserved:     1'b0
    //   RXDELAY:      3'b001           0.5 SCK = 3.33ns
    //   CLKDIV:       8'b0000_0010     150 / 2 = 75MHz

    // Revised PSRAM timings with 240MHz SCK, 4.167 ns per cycle.
    //   COOLDOWN:     2'b01            64 SCK = 266.7ns
    //   PAGEBREAK:    2'b10            break bursts at 1024-byte page boundaries
    //   reserved:     2'b00
    //   SELECT_SETUP: 1'b0             0.5 SCK = 2.08ns
    //   SELECT_HOLD:  2'b11            1.0 SCK = 4.17ns
    //   MAX_SELECT:   6'b01_1101       29 x 64 SCK = 7.734us
    //   MIN_DESELECT: 4'b1100          12.5 x SCK = 52.1ns
    //   reserved:     1'b0
    //   RXDELAY:      3'b010           1.0 SCK = 4.17ns
    //   CLKDIV:       8'b0000_0010     240 / 2 = 120MHz
    qmi_hw->m[1].timing = QMI_M0_TIMING_PAGEBREAK_VALUE_1024 << QMI_M0_TIMING_PAGEBREAK_LSB // Break between pages.
                          | 3 << QMI_M0_TIMING_SELECT_HOLD_LSB                              // Delay releasing CS for 3 extra system cycles.
                          | 1 << QMI_M0_TIMING_COOLDOWN_LSB
                          | 2 << QMI_M0_TIMING_RXDELAY_LSB
                          | 29 << QMI_M0_TIMING_MAX_SELECT_LSB   // In units of 64 system clock cycles. PSRAM says 8us max. 8 / 0.00752 / 64 = 16.62
                          | 12 << QMI_M0_TIMING_MIN_DESELECT_LSB // In units of system clock cycles. PSRAM says 50ns.50 / 7.52 = 6.64
                          | 2 << QMI_M0_TIMING_CLKDIV_LSB;
    qmi_hw->m[1].rfmt = (QMI_M0_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_PREFIX_WIDTH_LSB
                         | QMI_M0_RFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_RFMT_ADDR_WIDTH_LSB
                         | QMI_M0_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_SUFFIX_WIDTH_LSB
                         | QMI_M0_RFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_RFMT_DUMMY_WIDTH_LSB
                         | QMI_M0_RFMT_DUMMY_LEN_VALUE_24 << QMI_M0_RFMT_DUMMY_LEN_LSB
                         | QMI_M0_RFMT_DATA_WIDTH_VALUE_Q << QMI_M0_RFMT_DATA_WIDTH_LSB
                         | QMI_M0_RFMT_PREFIX_LEN_VALUE_8 << QMI_M0_RFMT_PREFIX_LEN_LSB
                         | QMI_M0_RFMT_SUFFIX_LEN_VALUE_NONE << QMI_M0_RFMT_SUFFIX_LEN_LSB);
    qmi_hw->m[1].rcmd = PSRAM_CMD_QUAD_READ << QMI_M0_RCMD_PREFIX_LSB
                        | 0 << QMI_M0_RCMD_SUFFIX_LSB;
    qmi_hw->m[1].wfmt = (QMI_M0_WFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_PREFIX_WIDTH_LSB
                         | QMI_M0_WFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_WFMT_ADDR_WIDTH_LSB
                         | QMI_M0_WFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_SUFFIX_WIDTH_LSB
                         | QMI_M0_WFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_WFMT_DUMMY_WIDTH_LSB
                         | QMI_M0_WFMT_DUMMY_LEN_VALUE_NONE << QMI_M0_WFMT_DUMMY_LEN_LSB
                         | QMI_M0_WFMT_DATA_WIDTH_VALUE_Q << QMI_M0_WFMT_DATA_WIDTH_LSB
                         | QMI_M0_WFMT_PREFIX_LEN_VALUE_8 << QMI_M0_WFMT_PREFIX_LEN_LSB
                         | QMI_M0_WFMT_SUFFIX_LEN_VALUE_NONE << QMI_M0_WFMT_SUFFIX_LEN_LSB);
    qmi_hw->m[1].wcmd = PSRAM_CMD_QUAD_WRITE << QMI_M0_WCMD_PREFIX_LSB
                        | 0 << QMI_M0_WCMD_SUFFIX_LSB;

    restore_interrupts(save_irq_status);

    psram_size[bank] = 1024 * 1024; // 1 MiB
    uint8_t size_id = eid >> 5;
    if (eid == 0x26 || size_id == 2)
    {
        psram_size[bank] *= 8;
    }
    else if (size_id == 0)
    {
        psram_size[bank] *= 2;
    }
    else if (size_id == 1)
    {
        psram_size[bank] *= 4;
    }

    // Mark that we can write to PSRAM.
    xip_ctrl_hw->ctrl |= XIP_CTRL_WRITABLE_M1_BITS;

    // Test write to the PSRAM.
    volatile uint32_t *psram_nocache = (volatile uint32_t *)XIP_PSRAM_NOCACHE;
    psram_nocache[0] = 0x12345678;
    volatile uint32_t readback = psram_nocache[0];
    if (readback != 0x12345678)
    {
        psram_size[bank] = 0;
        return;
    }
}

uint8_t xstack[XSTACK_SIZE + 1];
size_t volatile xstack_ptr;

uint8_t mbuf[MBUF_SIZE] __attribute__((aligned(4)));
size_t mbuf_len;

inline uint32_t mbuf_crc32(void)
{
    // use littlefs library
    return ~lfs_crc(~0, mbuf, mbuf_len);
}

void mem_init(void)
{
    // PSRAM Bank-Select pin
    gpio_init(QMI_PSRAM_BS_PIN);
    gpio_set_dir(QMI_PSRAM_BS_PIN, true);
    gpio_set_pulls(QMI_PSRAM_BS_PIN, false, false);

    // Select BANK0 for now
    mem_use_bank(0);
}

void mem_use_bank(uint8_t bank)
{
    gpio_put(QMI_PSRAM_BS_PIN, (bool)bank);
}

void mem_post_reclock(void)
{
    // Setup PSRAM controller and chips
    for (uint8_t bank = 0; bank < PSRAM_BANKS_NO; bank++)
    {
        mem_use_bank(bank);
        setup_psram(bank);
    }
    mem_use_bank(0);
}

void mem_read_buf(uint32_t addr)
{
    for (size_t i = 0; i < mbuf_len; ++i, ++addr)
    {
        if (addr >= 0xFFC0 && addr < 0x10000)
        {
            mbuf[i] = REGS(addr);
        }
        else
        {
            mbuf[i] = psram[addr & 0xFFFFFF];
        }
    }
}

void mem_write_buf(uint32_t addr)
{
    for (size_t i = 0; i < mbuf_len; ++i, ++addr)
    {
        if (addr >= 0xFFC0 && addr < 0x10000)
        {
            REGS(addr) = mbuf[i];
        }
        else
        {
            psram[addr & 0xFFFFFF] = mbuf[i];
        }
    }
}

void mem_print_status(void)
{
    int total_ram_size = 0;
    for (uint8_t bank = 0; bank < PSRAM_BANKS_NO; bank++)
    {
        total_ram_size += psram_size[bank];
    }
    printf("RAM : %dMB, %d banks\n", total_ram_size / (1024 * 1024), PSRAM_BANKS_NO);

    for (uint8_t bank = 0; bank < PSRAM_BANKS_NO; bank++)
    {
        printf("RAM%d: ", bank);
        if (psram_size[bank] == 0)
        {
            printf("[invalid]\n");
        }
        else
        {
            printf("%dMB%s (%llx)%s\n", psram_size[bank] / (1024 * 1024),
                   psram_readid_response[bank][0] == PSRAM_MF_AP ? " AP Memory" : "",
                   *((uint64_t *)psram_readid_response[bank]) & 0xffffffffffff,
                   psram_readid_response[bank][1] != PSRAM_KGD ? " [FAIL]" : "");
        }
    }
}
