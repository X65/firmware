/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "main.h"
#include "sys/aud.h"
#include "sys/com.h"
#include "sys/led.h"
#include "usb/cdc.h"
#include "usb/usb.h"
#include <pico/stdlib.h>

static void init(void)
{
    com_init();
    usb_init();
    aud_init();
    led_init();

    led_blink(true);
}

static void task(void)
{
    cdc_task();
    usb_task();
    aud_task();
    led_task();
}

int main()
{
    init();
    while (true)
        task();

    __builtin_unreachable();
}
