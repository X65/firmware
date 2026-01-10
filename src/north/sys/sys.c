/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/sys.h"
#include "api/clk.h"
#include "main.h"
#include "mon/mon.h"
#include "mon/str.h"
#include "net/ble.h"
#include "net/ntp.h"
#include "net/wfi.h"
#include "sys/cpu.h"
#include "sys/mem.h"
#include "sys/ria.h"
#include "sys/vpu.h"
#include "usb/msc.h"
#include "usb/usb.h"
#include "version.h"
#include <hardware/watchdog.h>
#include <pico/stdio.h>
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

#define X(name, value) \
    static const char __in_flash(STRINGIFY(name)) name[] = value;

X(STR_SYS_FULL_TERM_RESET, "\30\33c\n")
X(STR_SYS_DEBUG_TERM_RESET, "\30\33[0m\33[?25h\n\n")
#undef X

__in_flash("SYS_NAME") static const char SYS_NAME[] = RP6502_NAME "\n";

__in_flash("SYS_VERSION") static const char SYS_VERSION[] = "NBr "
#if !defined(RP6502_VERSION)
#if defined(GIT_TAG)
    GIT_TAG
#else
    GIT_REV "@" GIT_BRANCH
#endif
#else
                                                            "Version " RP6502_VERSION
#endif
#ifdef RP6502_RIA_W
                                                            " W"
#endif
                                                            "\n";

void sys_init(void)
{
#ifdef NDEBUG
    mon_add_response_str(STR_SYS_FULL_TERM_RESET);
#else
    mon_add_response_str(STR_SYS_DEBUG_TERM_RESET);
#endif
    mon_add_response_str(SYS_NAME);
    mon_add_response_str(SYS_VERSION);
    mon_add_response_fn(vpu_boot_response);
}

void sys_mon_reboot(const char *args, size_t len)
{
    (void)(args);
    (void)(len);
    stdio_flush();
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
    mon_add_response_str(SYS_NAME);
    mon_add_response_str(SYS_VERSION);
    mon_add_response_fn(vpu_boot_response);
    mon_add_response_fn(ria_status_response);
    mon_add_response_fn(vpu_status_response);
    mon_add_response_fn(cpu_status_response);
    mon_add_response_fn(ram_status_response);
    mon_add_response_fn(wfi_status_response);
    mon_add_response_fn(ntp_status_response);
    mon_add_response_fn(clk_status_response);
    mon_add_response_fn(ble_status_response);
    mon_add_response_fn(usb_status_response);
    mon_add_response_fn(msc_status_response);
}
