/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "main.h"
#include "sys/led.h"
#include "usb/usb.h"
#include <pico/stdlib.h>

static void init(void)
{
    usb_init();
    led_init();

    led_blink(true);
}

static void task(void)
{
    usb_task();
    led_task();
}

int main()
{
    init();
    while (true)
        task();

    __builtin_unreachable();
}
