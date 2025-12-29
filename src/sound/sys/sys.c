/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "version.h"
#include <pico.h>

__in_flash("snd_sys_sys") static const char SYS_VERSION[] = "SGU "
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
