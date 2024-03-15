/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "main.h"
#include "pico/stdlib.h"
#include "sys/led.h"
#include "sys/pix.h"
#include "sys/std.h"
#include "sys/vga.h"
#include "term/font.h"
#include "term/term.h"
#include "tusb.h"
#include "usb/cdc.h"
#include "usb/serno.h"

static void init(void)
{
    std_init();
    vga_init();
    font_init();
    term_init();
    serno_init(); // before tusb
    tusb_init();
    led_init();
    pix_init();
}

static void task(void)
{
    vga_task();
    term_task();
    tud_task();
    cdc_task();
    pix_task();
    led_task();
    std_task();
}

void main_flush(void)
{
}

void main_reclock(void)
{
    std_reclock();
}

void main()
{
    init();
    while (1)
        task();
}
