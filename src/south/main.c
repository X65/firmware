/*
 * Copyright (c) 2025 Rumbledethumps
 * Copyright (c) 2025 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "main.h"
#include "cgia/cgia.h"
#include "sys/buz.h"
#include "sys/com.h"
#include "sys/led.h"
#include "sys/out.h"
#include "sys/pix.h"
#include "term/font.h"
#include "term/term.h"
#include "usb/cdc.h"
#include "usb/usb.h"
#include <pico/stdlib.h>
#include <stdio.h>

static void init(void)
{
    com_init();
    cgia_init();
    font_init();
    term_init();
    usb_init();
    led_init();
    buz_init();
    pix_init();

    // Print startup message after setting code page
    // oem_init();
    printf("\n%s\n", RP6502_NAME);

    led_blink(true);

    // finally start video output
    out_init();
}

static void task(void)
{
    // com_task is important
    term_task();
    com_task();
    cdc_task();
    com_task();
    com_task();
    cgia_task();
    com_task();
    usb_task();
    com_task();
    pix_task();
    com_task();
    led_task();
    buz_task();

    // ext_task();
    // aud_task();
    // mdm_task();
}

int main(void)
{
    init();

    while (true)
        task();

    __builtin_unreachable();
}
