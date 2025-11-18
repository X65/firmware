/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/sys.h"
#include "api/clk.h"
#include "main.h"
#include "sys/cpu.h"
#include "sys/mem.h"
#include "sys/ria.h"
#include "sys/vga.h"
#include "usb/usb.h"
#include "version.h"
#include <hardware/watchdog.h>
#include <stdio.h>
#include <string.h>

#if defined(DEBUG_RIA_SYS) || defined(DEBUG_RIA_SYS_SYS)
#include <stdio.h>
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
static inline void DBG(const char *fmt, ...)
{
    (void)fmt;
}
#endif

__in_flash("ria_sys_sys") static const char SYS_VERSION[] = "RIA "
#if defined(GIT_TAG)
    GIT_TAG
#else
    GIT_REV "@" GIT_BRANCH
#endif
#ifdef RP6502_RIA_W
                                                            " W"
#endif
    ;

static void sys_print_status(void)
{
    puts(RP6502_NAME);
    puts(SYS_VERSION);
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
    ria_print_status();
    cpu_print_status();
    vga_print_status();
    mem_print_status();
    clk_print_status();
    usb_print_status();
}

void sys_init(void)
{
    // Reset terminal.
    puts("\30\33c");
    // Hello, world.
    sys_print_status();
    if (vga_connected())
        vga_print_status();
}
