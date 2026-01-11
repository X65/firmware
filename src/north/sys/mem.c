/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/mem.h"
#include "hw.h"
#include "littlefs/lfs_util.h"
#include "sys/pix.h"
#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/structs/qmi.h>
#include <hardware/structs/xip.h>
#include <hardware/sync.h>
#include <pico.h>
#include <stdio.h>

#if defined(DEBUG_RIA_SYS) || defined(DEBUG_RIA_SYS_MEM)
#include <stdio.h>
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
static inline void DBG(const char *fmt, ...)
{
    (void)fmt;
}
#endif

static void l2_init(void);

size_t psram_size[PSRAM_BANKS_NO];
uint8_t psram_readid_response[PSRAM_BANKS_NO][8];

// DETAILS/
//      apmemory APS6404L-3SQR-ZR
//      https://www.mouser.com/ProductDetail/AP-Memory/APS6404L-3SQR-ZR?qs=IS%252B4QmGtzzpDOdsCIglviw%3D%3D
//
// The origin of this logic is from the Circuit Python code that was downloaded from:
//     https://github.com/raspberrypi/pico-sdk-rp2350/issues/12#issuecomment-2055274428
//

// For PSRAM timing calculations - to use int math, we work in femto seconds (fs) (1e-15),
#define SEC_TO_FS 1000000000000000ll

// max select pulse width = 8us => 8e6 ns => 8000 ns => 8000 * 1e6 fs => 8000e6 fs
// Additionally, the MAX select is in units of 64 clock cycles - will use a constant that
// takes this into account - so 8000e6 fs / 64 = 125e6 fs
#define RP2350_PSRAM_MAX_SELECT_FS64 (125000000)

// min deselect pulse width = 50ns => 50 * 1e6 fs => 50e7 fs
#define RP2350_PSRAM_MIN_DESELECT_FS (50000000)

#define RP2350_PSRAM_RX_DELAY_FS (3333333)

// from psram datasheet - max Freq with VDDat 3.3v (Vcc = 3.0v +/- 10%)
const uint32_t RP2350_PSRAM_MAX_SCK_HZ = 133000000;

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

    // Get secs / cycle for the system clock - get before disabling interrupts.
    uint32_t sysHz = clock_get_hz(clk_sys);

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

    // Calculate the clock divider - goal to get clock used for PSRAM <= what
    // the PSRAM IC can handle - which is defined in RP2350_PSRAM_MAX_SCK_HZ
    volatile uint8_t clockDivider = (sysHz + RP2350_PSRAM_MAX_SCK_HZ - 1) / RP2350_PSRAM_MAX_SCK_HZ;

    // Get the clock femto seconds per cycle.
    uint32_t fsPerCycle = SEC_TO_FS / sysHz;
    uint32_t fsPerHalfCycle = fsPerCycle / 2;

    // the maxSelect value is defined in units of 64 clock cycles
    // So maxFS / (64 * fsPerCycle) = maxSelect = RP2350_PSRAM_MAX_SELECT_FS64/fsPerCycle
    volatile uint8_t maxSelect = RP2350_PSRAM_MAX_SELECT_FS64 / fsPerCycle;

    // minDeselect time - in system clock cycles
    // Must be higher than 50ns (min deselect time for PSRAM) so add a fsPerCycle - 1 to round up
    // So minFS/fsPerCycle = minDeselect = RP2350_PSRAM_MIN_DESELECT_FS/fsPerCycle

    volatile uint8_t minDeselect = (RP2350_PSRAM_MIN_DESELECT_FS + fsPerCycle - 1) / fsPerCycle;

    // RX delay (RP2350 datasheet 12.14.3.1) delay between between rising edge of SCK and
    // the start of RX sampling. Expressed in 0.5 system clock cycles. Smallest value
    // >= 3.3ns.
    volatile uint8_t rxDelay = (RP2350_PSRAM_RX_DELAY_FS + fsPerHalfCycle - 1) / fsPerHalfCycle;

    // printf("syshz=%u fsPerCycle=%u fsPerHalfCycle=%u RP2350_PSRAM_RX_DELAY_FS=%u\n", sysHz, fsPerCycle, fsPerHalfCycle, RP2350_PSRAM_RX_DELAY_FS);
    // printf("Max Select: %d, Min Deselect: %d, RX delay: %d, clock divider: %d\n", maxSelect, minDeselect, rxDelay, clockDivider);
    // printf("PSRAM clock rate %.1fMHz\n", (float)sysHz / clockDivider / 1e6);

    qmi_hw->m[1].timing = QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB // Break between pages.
                          | 3 << QMI_M1_TIMING_SELECT_HOLD_LSB                              // Delay releasing CS for 3 extra system cycles.
                          | 1 << QMI_M1_TIMING_COOLDOWN_LSB
                          | (rxDelay + clockDivider / 2) << QMI_M1_TIMING_RXDELAY_LSB // Delay between SCK and RX sampling.
                          | maxSelect << QMI_M1_TIMING_MAX_SELECT_LSB
                          | minDeselect << QMI_M1_TIMING_MIN_DESELECT_LSB
                          | clockDivider << QMI_M1_TIMING_CLKDIV_LSB;
    qmi_hw->m[1].rfmt = (QMI_M1_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M1_RFMT_PREFIX_WIDTH_LSB
                         | QMI_M1_RFMT_ADDR_WIDTH_VALUE_Q << QMI_M1_RFMT_ADDR_WIDTH_LSB
                         | QMI_M1_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M1_RFMT_SUFFIX_WIDTH_LSB
                         | QMI_M1_RFMT_DUMMY_WIDTH_VALUE_Q << QMI_M1_RFMT_DUMMY_WIDTH_LSB
                         | QMI_M1_RFMT_DUMMY_LEN_VALUE_24 << QMI_M1_RFMT_DUMMY_LEN_LSB
                         | QMI_M1_RFMT_DATA_WIDTH_VALUE_Q << QMI_M1_RFMT_DATA_WIDTH_LSB
                         | QMI_M1_RFMT_PREFIX_LEN_VALUE_8 << QMI_M1_RFMT_PREFIX_LEN_LSB
                         | QMI_M1_RFMT_SUFFIX_LEN_VALUE_NONE << QMI_M1_RFMT_SUFFIX_LEN_LSB);
    qmi_hw->m[1].rcmd = PSRAM_CMD_QUAD_READ << QMI_M1_RCMD_PREFIX_LSB
                        | 0 << QMI_M1_RCMD_SUFFIX_LSB;
    qmi_hw->m[1].wfmt = (QMI_M1_WFMT_PREFIX_WIDTH_VALUE_Q << QMI_M1_WFMT_PREFIX_WIDTH_LSB
                         | QMI_M1_WFMT_ADDR_WIDTH_VALUE_Q << QMI_M1_WFMT_ADDR_WIDTH_LSB
                         | QMI_M1_WFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M1_WFMT_SUFFIX_WIDTH_LSB
                         | QMI_M1_WFMT_DUMMY_WIDTH_VALUE_Q << QMI_M1_WFMT_DUMMY_WIDTH_LSB
                         | QMI_M1_WFMT_DUMMY_LEN_VALUE_NONE << QMI_M1_WFMT_DUMMY_LEN_LSB
                         | QMI_M1_WFMT_DATA_WIDTH_VALUE_Q << QMI_M1_WFMT_DATA_WIDTH_LSB
                         | QMI_M1_WFMT_PREFIX_LEN_VALUE_8 << QMI_M1_WFMT_PREFIX_LEN_LSB
                         | QMI_M1_WFMT_SUFFIX_LEN_VALUE_NONE << QMI_M1_WFMT_SUFFIX_LEN_LSB);
    qmi_hw->m[1].wcmd = PSRAM_CMD_QUAD_WRITE << QMI_M1_WCMD_PREFIX_LSB
                        | 0 << QMI_M1_WCMD_SUFFIX_LSB;

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

char response_buf[RESPONSE_BUF_SIZE];

uint8_t mbuf[MBUF_SIZE] __attribute__((aligned(4)));
size_t mbuf_len;

uint32_t mbuf_crc32(void)
{
    // use littlefs library
    return ~lfs_crc(~0, mbuf, mbuf_len);
}

volatile uint8_t __uninitialized_ram(regs)[0x40]
    __attribute__((aligned(0x40)));

static inline __force_inline void mem_select_bank(uint8_t bank)
{
    gpio_put(QMI_PSRAM_BS_PIN, (bool)bank);
}

void ram_init(void)
{
    // safety check for compiler alignment
    assert(!((uintptr_t)regs & 0x3F));

    // PSRAM Bank-Select pin
    gpio_init(QMI_PSRAM_BS_PIN);
    gpio_set_dir(QMI_PSRAM_BS_PIN, true);

    // Setup PSRAM controller and chips
    for (uint8_t bank = 0; bank < PSRAM_BANKS_NO; bank++)
    {
        mem_select_bank(bank);
        setup_psram(bank);
    }

    // Initialize L2 cache
    l2_init();
}

int ram_status_response(char *buf, size_t buf_size, int state)
{
    if (state < 0)
        return state;

    if (state == 0)
    {
        int total_ram_size = 0;
        for (uint8_t bank = 0; bank < PSRAM_BANKS_NO; bank++)
        {
            total_ram_size += psram_size[bank];
        }
        uint32_t sysHz = clock_get_hz(clk_sys);
        snprintf(buf, buf_size, "RAM : %dMB, %d banks, %.1fMHz\n",
                 total_ram_size / (1024 * 1024),
                 PSRAM_BANKS_NO,
                 (float)sysHz / (qmi_hw->m[1].timing & 0xFF) / 1e6);
        return 1;
    }

    const uint8_t bank = (uint8_t)(state - 1);
    if (bank >= PSRAM_BANKS_NO)
        return -1;

    if (psram_size[bank] == 0)
    {
        printf("RAM%d: [invalid]\n", bank);
    }
    else
    {
        printf("RAM%d: %dMB%s (%llx)%s\n", bank, psram_size[bank] / (1024 * 1024),
               psram_readid_response[bank][0] == PSRAM_MF_AP ? " AP Memory" : "",
               *((uint64_t *)psram_readid_response[bank]) & 0xffffffffffff,
               psram_readid_response[bank][1] != PSRAM_KGD ? " [FAIL]" : "");
    }

    return state + 1;
}

// ---------------------------------------------------------------
// L2 memory cache implementation
// ---------------------------------------------------------------
// Cache Size: 64 kB
// Cache Line Size: 32 Bytes [XIP fast fetch]
//
// We split the incoming 65C816 address into three parts to look up data:
// [AAAA AAAA][BBBB BBBB BBB][CCCCC]
// Offset (5 bits): Which of the 32 bytes in the line do we want?
// Index (11 bits): Which of the 2048 cache lines do we check? ($2^{11} = 2048$)
// Tag (8 bits): The remaining upper bits. We store this to verify if the cache
//               line actually holds the memory we asked for.

#define CACHE_LINE_SIZE  32
#define CACHE_LINE_COUNT 2048
#define CACHE_LINE_MASK  (CACHE_LINE_COUNT - 1) // 0x7FF
#define OFFSET_MASK      (CACHE_LINE_SIZE - 1)  // 0x1F
#define TAG_MASK         0xFF
#define TAG_VALID_BIT    0x100

// The Data Store: 64kB
uint8_t __attribute__((aligned(32)))
__uninitialized_ram(l2_data)[CACHE_LINE_COUNT][CACHE_LINE_SIZE];

// The Tag Store: 2048 entries
// We need to store the 8-bit Tag AND a "Valid" bit.
// A 16-bit int is faster to align/access than a packed byte struct.
uint16_t __attribute__((aligned(2)))
__uninitialized_ram(l2_tags)[CACHE_LINE_COUNT];

static void l2_init(void)
{
    // Invalidate all cache lines.
    for (size_t i = 0; i < CACHE_LINE_COUNT; i++)
    {
        l2_tags[i] = 0;
    }
}

__force_inline static void
fast_fill_32b(uint32_t *dest, const uint32_t *src_nocache)
{
    __asm volatile(
        "ldmia %1!, {r0-r3}\n\t" // Burst Load 4 words (16B) from PSRAM
        "stmia %0!, {r0-r3}\n\t" // Burst Store 4 words (16B) to SRAM
        "ldmia %1!, {r0-r3}\n\t" // Repeat to complete 32B
        "stmia %0!, {r0-r3}\n\t"
        : "+r"(dest), "+r"(src_nocache)    // Outputs (pointers define the address)
        :                                  // No other inputs
        : "r0", "r1", "r2", "r3", "memory" // Clobbers
    );
}

__force_inline uint8_t __attribute__((optimize("O3")))
__not_in_flash_func(mem_read_ram)(uint32_t addr24)
{
    const uint16_t index = (addr24 >> 5) & CACHE_LINE_MASK;
    const uint8_t tag = (addr24 >> 16) & TAG_MASK;
    const uint16_t stored_tag = l2_tags[index];
    if (((stored_tag & TAG_MASK) != tag) || !(stored_tag & TAG_VALID_BIT))
    {
        // Cache miss - fetch the cache line from PSRAM
        gpio_put(QMI_PSRAM_BS_PIN, addr24 & 0x800000);
        fast_fill_32b((uint32_t *)l2_data[index],
                      (const uint32_t *)(XIP_PSRAM_NOCACHE | (addr24 & 0x7FFFE0)));
        // Update the tag store
        l2_tags[index] = tag | TAG_VALID_BIT;
    }

    return l2_data[index][addr24 & OFFSET_MASK];
}

__force_inline void __attribute__((optimize("O3")))
__not_in_flash_func(mem_write_ram)(uint32_t addr24, uint8_t data)
{
    // L2 write-through cache
    gpio_put(QMI_PSRAM_BS_PIN, addr24 & 0x800000);
    *(volatile uint8_t *)(XIP_PSRAM_NOCACHE | (addr24 & 0x7FFFFF)) = data;

    // Update L2 cache if present
    const uint16_t index = (addr24 >> 5) & CACHE_LINE_MASK;
    const uint8_t tag = (addr24 >> 16) & TAG_MASK;
    const uint16_t stored_tag = l2_tags[index];
    if (((stored_tag & TAG_MASK) == tag) && (stored_tag & TAG_VALID_BIT))
    {
        l2_data[index][addr24 & OFFSET_MASK] = data;
    }

    // Sync write to CGIA L1 cache
    pix_mem_write(addr24, data);
}

// Buffer for DMA line fetches.
// Use separate buffer to avoid cache pollution.
uint8_t __attribute__((aligned(32)))
__uninitialized_ram(fetch_row_data)[CACHE_LINE_SIZE];

uint8_t *__attribute__((optimize("O3")))
__not_in_flash_func(mem_fetch_row)(uint8_t bank, uint16_t addr)
{
    const uint32_t addr24 = bank << 16 | addr;
    // Fetch the line from PSRAM
    mem_select_bank((addr24 >> 23) & 0x01);
    fast_fill_32b((uint32_t *)fetch_row_data,
                  (const uint32_t *)(XIP_PSRAM_NOCACHE | (addr24 & 0x7FFFE0)));
    return fetch_row_data;
}
