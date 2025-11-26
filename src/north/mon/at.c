/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "at.h"

#include "sys/mdm.h"
#include <pico/platform/compiler.h>
#include <stdio.h>
#include <string.h>

static char cmd_buf[256];

void at_mon_at(const char *args, size_t len)
{
    cmd_buf[0] = 'A';
    cmd_buf[1] = 'T';
    size_t send_size = 2;
    if (len)
    {
        size_t cpy_len = MIN(len, sizeof(cmd_buf) - 4);
        memcpy(cmd_buf + 2, args, cpy_len);
        send_size = cpy_len + 2;
    }
    cmd_buf[send_size++] = '\r';
    cmd_buf[send_size++] = '\n';

    int32_t ret = -1; // mdm_write_data_to_slave((uint8_t *)cmd_buf, send_size);
    if (ret == -1)
    {
        printf("?not ready\n");
    }
}
