/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/ext.h"
#include "sys/out.h"
#include "version.h"
#include <pico.h>

__in_flash("sb_sys_sys") static const char SYS_VERSION[] = "SBr "
#if defined(GIT_TAG)
    GIT_TAG
#else
    GIT_REV "@" GIT_BRANCH
#endif
    ;

__in_flash("sys_version") const char *sys_version(void)
{
    return SYS_VERSION;
}

void sys_write_status(void)
{
    out_write_status();
    // aud_print_status();
    // gpx_dump_registers();
    // ext_bus_scan();
}
