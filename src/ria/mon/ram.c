/*
 * Copyright (c) 2023 Rumbledethumps
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "str.h"
#include "sys/com.h"
#include "sys/mem.h"
#include <stdio.h>

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
