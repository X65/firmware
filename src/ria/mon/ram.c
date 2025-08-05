/*
 * Copyright (c) 2023 Rumbledethumps
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "main.h"
#include "str.h"
#include "sys/com.h"
#include "sys/mem.h"
#include <pico/rand.h>
#include <pico/stdio.h>
#include <pico/time.h>
#include <stdio.h>
#include <stdlib.h>

#define TIMEOUT_MS 200

bool is_active = false;

static uint32_t rw_addr;
static uint32_t rw_len;
static uint32_t rw_crc;

// Commands that start with a hex address. Read or write memory.
void ram_mon_address(const char *args, size_t len)
{
    // addr syntax is already validated by dispatch
    rw_addr = 0;
    size_t i = 0;
    for (; i < len; i++)
    {
        char ch = args[i];
        if (char_is_hex(ch))
            rw_addr = rw_addr * 16 + char_to_int(ch);
        else
            break;
    }
    for (; i < len; i++)
        if (args[i] != ' ')
            break;
    if (rw_addr > 0xFFFFFF)
    {
        printf("?invalid address\n");
        return;
    }
    if (i == len)
    {
        mbuf_len = (rw_addr | 0xF) - rw_addr + 1;
        mem_read_buf(rw_addr);
        printf("%04lX", rw_addr);
        for (size_t i = 0; i < mbuf_len; i++)
            printf(" %02X", mbuf[i]);
        printf("\n");
        return;
    }
    uint32_t data = 0x80000000;
    mbuf_len = 0;
    for (; i < len; i++)
    {
        char ch = args[i];
        if (char_is_hex(ch))
            data = data * 16 + char_to_int(ch);
        else if (ch != ' ')
        {
            printf("?invalid data character\n");
            return;
        }
        if (ch == ' ' || i == len - 1)
        {
            if (data < 0x100)
            {
                mbuf[mbuf_len++] = data;
                data = 0x80000000;
            }
            else
            {
                printf("?invalid data value\n");
                return;
            }
            for (; i + 1 < len; i++)
                if (args[i + 1] != ' ')
                    break;
        }
    }
    mem_write_buf(rw_addr);
}

static void sys_com_rx_mbuf(bool timeout, const char *buf, size_t length)
{
    mbuf_len = length;
    is_active = false;

    if (timeout)
    {
        puts("?timeout");
        return;
    }
    if (mbuf_crc32() != rw_crc)
    {
        puts("?CRC does not match");
        return;
    }

    uint32_t addr = rw_addr;
    while (length--)
    {
        // FIXME: should use mem_write_buf() instead of writing directly to psram
        printf("rx_mbuf: 0x%06lX <- 0x%02X\n", addr, *buf);
        psram[addr++ & 0xFFFFFF] = *(buf++);
    }
}

void ram_mon_binary(const char *args, size_t len)
{
    if (parse_uint32(&args, &len, &rw_addr) && parse_uint32(&args, &len, &rw_len) && parse_uint32(&args, &len, &rw_crc) && parse_end(args, len))
    {
        if (rw_addr > 0xFFFFFF)
        {
            printf("?invalid address\n");
            return;
        }
        if (!rw_len || rw_len > MBUF_SIZE)
        {
            printf("?invalid length\n");
            return;
        }
        is_active = true;
        com_read_binary(TIMEOUT_MS, sys_com_rx_mbuf, mbuf, rw_len);
        return;
    }
    printf("?invalid argument\n");
}

bool ram_active(void)
{
    return is_active;
}

// ------------------ MEMTEST --------------------
#include "hardware/regs/addressmap.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/xip_cache.h"
#include "pico/rand.h"

#define BUF_SIZE            (128 * 1024)
#define FLASH_TARGET_OFFSET (1024 * 1024) // +1MB should be safe to use

static volatile uint32_t random_buf[BUF_SIZE / 4];
static volatile uint32_t copy_buf[BUF_SIZE / 4];

// Flash erase and program function trampolines
static bool verify_buffer(const uint32_t *src, const uint32_t *dst, size_t len)
{
    for (size_t i = 0; i < len / 4; i++)
    {
        if (src[i] != dst[i])
        {
            printf("Buffer mismatch at index %d: %08x != %08x\n", i, src[i], dst[i]);
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
            printf("ERROR : [%d] != [%d]\n", source_data[i] + offset, data_buffer[i]);
            return false;
        }
    }
    return true;
}

void ram_mon_test(const char *args, size_t len)
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
    printf("SRAM buffer random filled in %d us\n", end_time - start_time);

    for (uint8_t bank = 0; bank < PSRAM_BANKS_NO; bank++)
    {
        if (psram_size[bank] > 0)
        {
            printf("\n------- PSRAM BANK%d -------\n", bank);
            mem_use_bank(bank);

            // Copied from: https://gist.github.com/eightycc/b61813c05899281ce7d2a2f86490be3b
            printf("PSRAM size: %d bytes\n", psram_size[bank]);
            printf("PSRAM read ID: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                   psram_readid_response[0][bank], psram_readid_response[1][bank], psram_readid_response[2][bank], psram_readid_response[3][bank],
                   psram_readid_response[4][bank], psram_readid_response[5][bank], psram_readid_response[6][bank], psram_readid_response[7][bank]);
            printf("\n");

            // Copy SRAM -> SRAM and verify
            uint32_t copy_time = time_copy_buffer((const uint32_t *)random_buf, (uint32_t *)copy_buf, BUF_SIZE);
            printf("SRAM -> SRAM copied in %d us, %d ns/word\n", copy_time, (copy_time * 1000) / (BUF_SIZE / 4));
            if (!verify_buffer((const uint32_t *)random_buf, (const uint32_t *)copy_buf, BUF_SIZE))
            {
                printf("Buffer verification failed\n");
            }

            // Copy Flash(cached) -> SRAM and verify
            xip_cache_clean_all();
            copy_time = time_copy_buffer((const uint32_t *)random_buf, (uint32_t *)XIP_BASE + FLASH_TARGET_OFFSET, BUF_SIZE);
            printf("Flash(cached) -> SRAM copied in %d us, %d ns/word\n", copy_time, (copy_time * 1000) / (BUF_SIZE / 4));
            if (!verify_buffer((const uint32_t *)random_buf, (const uint32_t *)copy_buf, BUF_SIZE))
            {
                printf("Buffer verification failed\n");
            }

            // Copy Flash(no cache) -> SRAM and verify
            xip_cache_clean_all();
            copy_time = time_copy_buffer((const uint32_t *)random_buf, (uint32_t *)XIP_NOCACHE_NOALLOC_BASE + FLASH_TARGET_OFFSET, BUF_SIZE);
            printf("Flash(no cache) -> SRAM copied in %d us, %d ns/word\n", copy_time, (copy_time * 1000) / (BUF_SIZE / 4));
            if (!verify_buffer((const uint32_t *)random_buf, (const uint32_t *)copy_buf, BUF_SIZE))
            {
                printf("Buffer verification failed\n");
            }

            // Copy SRAM -> PSRAM(cached) and verify
            xip_cache_clean_all();
            copy_time = time_copy_buffer((const uint32_t *)random_buf, (uint32_t *)XIP_PSRAM_CACHED, BUF_SIZE);
            printf("SRAM -> PSRAM(cached) copied in %d us, %d ns/word\n", copy_time, (copy_time * 1000) / (BUF_SIZE / 4));
            if (!verify_buffer((const uint32_t *)random_buf, (const uint32_t *)XIP_PSRAM_CACHED, BUF_SIZE))
            {
                printf("Buffer verification failed\n");
            }

            // Copy SRAM -> PSRAM(no cache) and verify
            xip_cache_clean_all();
            copy_time = time_copy_buffer((const uint32_t *)random_buf, (uint32_t *)XIP_PSRAM_NOCACHE, BUF_SIZE);
            printf("SRAM -> PSRAM(no cache) copied in %d us, %d ns/word\n", copy_time, (copy_time * 1000) / (BUF_SIZE / 4));
            if (!verify_buffer((const uint32_t *)random_buf, (const uint32_t *)XIP_PSRAM_NOCACHE, BUF_SIZE))
            {
                printf("Buffer verification failed\n");
            }

            // Copy PSRAM(cached) -> SRAM and verify
            xip_cache_clean_all();
            time_copy_buffer((const uint32_t *)random_buf, (uint32_t *)XIP_PSRAM_NOCACHE, BUF_SIZE);
            xip_cache_clean_all();
            copy_time = time_copy_buffer((const uint32_t *)XIP_PSRAM_CACHED, (uint32_t *)copy_buf, BUF_SIZE);
            printf("PSRAM(cached) -> SRAM copied in %d us, %d ns/word\n", copy_time, (copy_time * 1000) / (BUF_SIZE / 4));
            if (!verify_buffer((const uint32_t *)random_buf, (const uint32_t *)copy_buf, BUF_SIZE))
            {
                printf("Buffer verification failed\n");
            }

            // Copy PSRAM(no cache) -> SRAM and verify
            xip_cache_clean_all();
            time_copy_buffer((const uint32_t *)random_buf, (uint32_t *)XIP_PSRAM_NOCACHE, BUF_SIZE);
            xip_cache_clean_all();
            copy_time = time_copy_buffer((const uint32_t *)XIP_PSRAM_NOCACHE, (uint32_t *)copy_buf, BUF_SIZE);
            printf("PSRAM(no cache) -> SRAM copied in %d us, %d ns/word\n", copy_time, (copy_time * 1000) / (BUF_SIZE / 4));
            if (!verify_buffer((const uint32_t *)random_buf, (const uint32_t *)copy_buf, BUF_SIZE))
            {
                printf("Buffer verification failed\n");
            }

            // Copy PSRAM(cached) -> PSRAM(cached) and verify
            xip_cache_clean_all();
            time_copy_buffer((const uint32_t *)random_buf, (uint32_t *)XIP_PSRAM_NOCACHE, BUF_SIZE);
            xip_cache_clean_all();
            copy_time = time_copy_buffer((const uint32_t *)XIP_PSRAM_CACHED, (uint32_t *)XIP_PSRAM_CACHED + BUF_SIZE, BUF_SIZE);
            printf("PSRAM(cached) -> PSRAM(cached) copied in %d us, %d ns/word\n", copy_time, (copy_time * 1000) / (BUF_SIZE / 4));
            if (!verify_buffer((const uint32_t *)random_buf, (const uint32_t *)XIP_PSRAM_CACHED + BUF_SIZE, BUF_SIZE))
            {
                printf("Buffer verification failed\n");
            }

            // Copy PSRAM(no cache) -> PSRAM(no cache) and verify
            xip_cache_clean_all();
            time_copy_buffer((const uint32_t *)random_buf, (uint32_t *)XIP_PSRAM_NOCACHE, BUF_SIZE);
            xip_cache_clean_all();
            copy_time = time_copy_buffer((const uint32_t *)XIP_PSRAM_NOCACHE, (uint32_t *)XIP_PSRAM_NOCACHE + BUF_SIZE, BUF_SIZE);
            printf("PSRAM(no cache) -> PSRAM(no cache) copied in %d us, %d ns/word\n", copy_time, (copy_time * 1000) / (BUF_SIZE / 4));
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

        mem_use_bank(0);
        psram_nocache[0] = 0x56A90FF0;
        mem_use_bank(1);
        psram_nocache[0] = 0xF00F659A;

        mem_use_bank(0);
        volatile uint32_t readback0 = psram_nocache[0];
        mem_use_bank(1);
        volatile uint32_t readback1 = psram_nocache[0];
        if (readback0 == readback1)
        {
            printf("\nCross-bank test failed: same data in both banks [%08X]\n", readback0);
        }
    }
    mem_use_bank(0);
}
