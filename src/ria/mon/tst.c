/*
 * Copyright (c) 2025 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mon/tst.h"
#include "hw.h"
#include "sys/mem.h"
#include <hardware/gpio.h>
#include <hardware/regs/addressmap.h>
#include <hardware/structs/xip_ctrl.h>
#include <hardware/timer.h>
#include <hardware/xip_cache.h>
#include <pico/rand.h>
#include <pico/stdio.h>
#include <stdio.h>

// Reach for other modules internals
// (I don't really want to expose these in global headers)
extern volatile uint8_t mem_cache[];
extern size_t psram_size[PSRAM_BANKS_NO];
extern uint8_t psram_readid_response[PSRAM_BANKS_NO][8];

#define BUF_SIZE            (64 * 1024)   // 64KB buffer
#define FLASH_TARGET_OFFSET (1024 * 1024) // +1MB should be safe to use

volatile uint32_t *random_buf = (volatile uint32_t *)mem_cache;
volatile uint32_t *copy_buf = (volatile uint32_t *)(mem_cache + BUF_SIZE);

// Flash erase and program function trampolines
static bool verify_buffer(const uint32_t *src, const uint32_t *dst, size_t len)
{
    for (size_t i = 0; i < len / 4; i++)
    {
        if (src[i] != dst[i])
        {
            printf("Buffer mismatch at index %d: %08lx != %08lx\n", i, src[i], dst[i]);
            return false;
        }
    }
    return true;
}

static uint32_t time_copy_buffer(const uint32_t *src, uint32_t *dst, size_t len)
{
    uint32_t start_time = time_us_32();
    for (int i = 0; i < (len / 4); i++)
    {
        dst[i] = src[i];
    }
    uint32_t end_time = time_us_32();
    return end_time - start_time;
}

#define DATA_ELEMENTS   4096
#define DATA_BLOCK_SIZE (DATA_ELEMENTS * sizeof(int32_t))
static_assert(BUF_SIZE >= DATA_BLOCK_SIZE);

static void erase_data_block(int32_t *data_buffer)
{
    for (size_t i = 0; i < DATA_ELEMENTS; i++)
        data_buffer[i] = 0xFFFFFFFF;
}
static void write_data_block(int32_t *source_data, int32_t *data_buffer, int32_t offset)
{
    for (size_t i = 0; i < DATA_ELEMENTS; i++)
        data_buffer[i] = source_data[i] + offset;
}

static bool check_data_block(int32_t *source_data, int32_t *data_buffer, int32_t offset)
{
    for (size_t i = 0; i < DATA_ELEMENTS; i++)
    {
        if (source_data[i] + offset != data_buffer[i])
        {
            printf("ERROR : [%ld] != [%ld]\n", source_data[i] + offset, data_buffer[i]);
            return false;
        }
    }
    return true;
}

static inline __attribute__((always_inline)) void mem_select_bank(uint8_t bank)
{
    gpio_put(QMI_PSRAM_BS_PIN, (bool)bank);
}

void tst_mon_memtest(const char *args, size_t len)
{
    (void)(args);
    (void)(len);

    printf("Buffer size: %d bytes\n", BUF_SIZE);

    // Fill an SRAM buffer with a random pattern
    uint32_t start_time = time_us_32();
    for (int i = 0; i < (BUF_SIZE / 4); i++)
    {
        ((uint32_t *)random_buf)[i] = get_rand_32();
    }
    uint32_t end_time = time_us_32();
    printf("SRAM buffer random filled in %ld us\n", end_time - start_time);

    for (uint8_t bank = 0; bank < PSRAM_BANKS_NO; bank++)
    {
        if (psram_size[bank] > 0)
        {
            printf("\n------- PSRAM BANK%d -------\n", bank);
            mem_select_bank(bank);

            // Copied from: https://gist.github.com/eightycc/b61813c05899281ce7d2a2f86490be3b
            printf("PSRAM size: %d bytes\n", psram_size[bank]);
            printf("PSRAM read ID: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                   psram_readid_response[bank][0], psram_readid_response[bank][1], psram_readid_response[bank][2], psram_readid_response[bank][3],
                   psram_readid_response[bank][4], psram_readid_response[bank][5], psram_readid_response[bank][6], psram_readid_response[bank][7]);
            printf("\n");

            // Copy SRAM -> SRAM and verify
            uint32_t copy_time = time_copy_buffer((const uint32_t *)random_buf, (uint32_t *)copy_buf, BUF_SIZE);
            printf("SRAM -> SRAM copied in %ld us, %ld ns/word\n", copy_time, (copy_time * 1000) / (BUF_SIZE / 4));
            if (!verify_buffer((const uint32_t *)random_buf, (const uint32_t *)copy_buf, BUF_SIZE))
            {
                printf("Buffer verification failed\n");
            }

            // Copy Flash(cached) -> SRAM and verify
            xip_cache_clean_all();
            copy_time = time_copy_buffer((const uint32_t *)random_buf, (uint32_t *)XIP_BASE + FLASH_TARGET_OFFSET, BUF_SIZE);
            printf("Flash(cached) -> SRAM copied in %ld us, %ld ns/word\n", copy_time, (copy_time * 1000) / (BUF_SIZE / 4));
            if (!verify_buffer((const uint32_t *)random_buf, (const uint32_t *)copy_buf, BUF_SIZE))
            {
                printf("Buffer verification failed\n");
            }

            // Copy Flash(no cache) -> SRAM and verify
            xip_cache_clean_all();
            copy_time = time_copy_buffer((const uint32_t *)random_buf, (uint32_t *)XIP_NOCACHE_NOALLOC_BASE + FLASH_TARGET_OFFSET, BUF_SIZE);
            printf("Flash(no cache) -> SRAM copied in %ld us, %ld ns/word\n", copy_time, (copy_time * 1000) / (BUF_SIZE / 4));
            if (!verify_buffer((const uint32_t *)random_buf, (const uint32_t *)copy_buf, BUF_SIZE))
            {
                printf("Buffer verification failed\n");
            }

            // Copy SRAM -> PSRAM(cached) and verify
            xip_cache_clean_all();
            copy_time = time_copy_buffer((const uint32_t *)random_buf, (uint32_t *)XIP_PSRAM_CACHED, BUF_SIZE);
            printf("SRAM -> PSRAM(cached) copied in %ld us, %ld ns/word\n", copy_time, (copy_time * 1000) / (BUF_SIZE / 4));
            if (!verify_buffer((const uint32_t *)random_buf, (const uint32_t *)XIP_PSRAM_CACHED, BUF_SIZE))
            {
                printf("Buffer verification failed\n");
            }

            // Copy SRAM -> PSRAM(no cache) and verify
            xip_cache_clean_all();
            copy_time = time_copy_buffer((const uint32_t *)random_buf, (uint32_t *)XIP_PSRAM_NOCACHE, BUF_SIZE);
            printf("SRAM -> PSRAM(no cache) copied in %ld us, %ld ns/word\n", copy_time, (copy_time * 1000) / (BUF_SIZE / 4));
            if (!verify_buffer((const uint32_t *)random_buf, (const uint32_t *)XIP_PSRAM_NOCACHE, BUF_SIZE))
            {
                printf("Buffer verification failed\n");
            }

            // Copy PSRAM(cached) -> SRAM and verify
            xip_cache_clean_all();
            time_copy_buffer((const uint32_t *)random_buf, (uint32_t *)XIP_PSRAM_NOCACHE, BUF_SIZE);
            xip_cache_clean_all();
            copy_time = time_copy_buffer((const uint32_t *)XIP_PSRAM_CACHED, (uint32_t *)copy_buf, BUF_SIZE);
            printf("PSRAM(cached) -> SRAM copied in %ld us, %ld ns/word\n", copy_time, (copy_time * 1000) / (BUF_SIZE / 4));
            if (!verify_buffer((const uint32_t *)random_buf, (const uint32_t *)copy_buf, BUF_SIZE))
            {
                printf("Buffer verification failed\n");
            }

            // Copy PSRAM(no cache) -> SRAM and verify
            xip_cache_clean_all();
            time_copy_buffer((const uint32_t *)random_buf, (uint32_t *)XIP_PSRAM_NOCACHE, BUF_SIZE);
            xip_cache_clean_all();
            copy_time = time_copy_buffer((const uint32_t *)XIP_PSRAM_NOCACHE, (uint32_t *)copy_buf, BUF_SIZE);
            printf("PSRAM(no cache) -> SRAM copied in %ld us, %ld ns/word\n", copy_time, (copy_time * 1000) / (BUF_SIZE / 4));
            if (!verify_buffer((const uint32_t *)random_buf, (const uint32_t *)copy_buf, BUF_SIZE))
            {
                printf("Buffer verification failed\n");
            }

            // Copy PSRAM(cached) -> PSRAM(cached) and verify
            xip_cache_clean_all();
            time_copy_buffer((const uint32_t *)random_buf, (uint32_t *)XIP_PSRAM_NOCACHE, BUF_SIZE);
            xip_cache_clean_all();
            copy_time = time_copy_buffer((const uint32_t *)XIP_PSRAM_CACHED, (uint32_t *)XIP_PSRAM_CACHED + BUF_SIZE, BUF_SIZE);
            printf("PSRAM(cached) -> PSRAM(cached) copied in %ld us, %ld ns/word\n", copy_time, (copy_time * 1000) / (BUF_SIZE / 4));
            if (!verify_buffer((const uint32_t *)random_buf, (const uint32_t *)XIP_PSRAM_CACHED + BUF_SIZE, BUF_SIZE))
            {
                printf("Buffer verification failed\n");
            }

            // Copy PSRAM(no cache) -> PSRAM(no cache) and verify
            xip_cache_clean_all();
            time_copy_buffer((const uint32_t *)random_buf, (uint32_t *)XIP_PSRAM_NOCACHE, BUF_SIZE);
            xip_cache_clean_all();
            copy_time = time_copy_buffer((const uint32_t *)XIP_PSRAM_NOCACHE, (uint32_t *)XIP_PSRAM_NOCACHE + BUF_SIZE, BUF_SIZE);
            printf("PSRAM(no cache) -> PSRAM(no cache) copied in %ld us, %ld ns/word\n", copy_time, (copy_time * 1000) / (BUF_SIZE / 4));
            if (!verify_buffer((const uint32_t *)random_buf, (const uint32_t *)XIP_PSRAM_NOCACHE + BUF_SIZE, BUF_SIZE))
            {
                printf("Buffer verification failed\n");
            }

            printf("\n");

            // Copied from: https://github.com/sparkfun/sparkfun-pico/blob/main/examples/has_psram/has_psram.c

            // How many data blocks to write - use all of PSRAM
            size_t nDataBlocks = psram_size[bank] / DATA_BLOCK_SIZE;
            printf("PSRAM data block testing - %d data blocks\n", nDataBlocks);

            // Write data blocks to PSRAM - then read them back and check
            for (size_t i = 0; i < nDataBlocks; i++)
            {
                int32_t *data_buffer = (int32_t *)(XIP_PSRAM_CACHED + i * DATA_BLOCK_SIZE);
                erase_data_block(data_buffer);
                write_data_block(random_buf, data_buffer, i * 0x2);
            }
            printf("Data blocks written\n");

            int nPassed = 0;
            for (size_t i = 0; i < nDataBlocks; i++)
            {
                if (i % 10 == 0)
                {
                    printf(".");
                    stdio_flush();
                }
                int32_t *data_buffer = (int32_t *)(XIP_PSRAM_CACHED + i * DATA_BLOCK_SIZE);
                if (!check_data_block(random_buf, data_buffer, i * 0x2))
                    printf("Data block %d failed\n", (int)i);
                else
                    nPassed++;
            }

            printf("\nTest Run: %d, Passed: %d, Failed: %d\n", nDataBlocks, nPassed, nDataBlocks - nPassed);
        }
    }

    // cross-bank test - should not have the same data in both banks
    if (psram_size[0] > 0 && psram_size[1] > 0)
    {
        volatile uint32_t *psram_nocache = (volatile uint32_t *)XIP_PSRAM_NOCACHE;

        mem_select_bank(0);
        psram_nocache[0] = 0x56A90FF0;
        mem_select_bank(1);
        psram_nocache[0] = 0xF00F659A;

        mem_select_bank(0);
        volatile uint32_t readback0 = psram_nocache[0];
        mem_select_bank(1);
        volatile uint32_t readback1 = psram_nocache[0];
        if (readback0 == readback1)
        {
            printf("\nCross-bank test failed: same data in both banks [%08lX]\n", readback0);
        }
    }
    mem_select_bank(0);
}
