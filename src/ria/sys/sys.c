/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/sys.h"
#include "hardware/watchdog.h"
#include "main.h"
#include "pico/stdlib.h"
#include "sys/mem.h"
#include "usb/usb.h"
#include <stdio.h>
#include <string.h>

extern const char *GIT_TAG;
extern const char *GIT_REV;
extern const char *GIT_BRANCH;

static void sys_print_status(void)
{
    puts(RP6502_NAME);
    if (strlen(GIT_TAG))
        printf("RIA %s", GIT_TAG);
    else
        printf("RIA %s@%s", GIT_REV, GIT_BRANCH);
}

void sys_mon_reboot(const char *args, size_t len)
{
    (void)(args);
    (void)(len);
    watchdog_reboot(0, 0, 0);
}

void sys_mon_reset(const char *args, size_t len)
{
    (void)(args);
    (void)(len);
    main_run();
}

void sys_mon_status(const char *args, size_t len)
{
    (void)(args);
    (void)(len);
    sys_print_status();
    mem_print_status();
    // vga_print_status();
    usb_print_status();
}

void sys_init(void)
{
    // Reset terminal.
    puts("\30\33[0m\f");
    // Hello, world.
    sys_print_status();
}
