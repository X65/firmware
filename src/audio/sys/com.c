/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/com.h"
#include "hw.h"
#include "sys/sys.h"
#include "usb/cdc.h"
#include <pico/stdio/driver.h>
#include <pico/stdlib.h>
#include <stdio.h>
#include <tusb.h>

size_t com_out_head;
size_t com_out_tail;
char com_out_buf[COM_OUT_BUF_SIZE];

bool com_out_empty(void)
{
    return com_out_head == com_out_tail;
}

char com_out_peek(void)
{
    return com_out_buf[(com_out_tail + 1) % COM_OUT_BUF_SIZE];
}

char com_out_read(void)
{
    com_out_tail = (com_out_tail + 1) % COM_OUT_BUF_SIZE;
    return com_out_buf[com_out_tail];
}

static void com_out_chars(const char *buf, int length)
{
    while (length)
    {
        while (length && ((com_out_head + 1) % COM_OUT_BUF_SIZE) != com_out_tail)
        {
            com_out_head = (com_out_head + 1) % COM_OUT_BUF_SIZE;
            com_out_buf[com_out_head] = *buf++;
            length--;
        }
        if (((com_out_head + 1) % COM_OUT_BUF_SIZE) == com_out_tail)
        {
            cdc_task();
            tud_task();
        }
    }
}

void com_init(void)
{
    stdio_uart_init_full(COM_UART_INTERFACE, COM_UART_BAUDRATE, COM_UART_TX_PIN, COM_UART_RX_PIN);

    static stdio_driver_t stdio_driver = {
        .out_chars = com_out_chars,
        .crlf_enabled = true,
    };
    stdio_set_driver_enabled(&stdio_driver, true);

    printf("\n%s\n", sys_version());
}
