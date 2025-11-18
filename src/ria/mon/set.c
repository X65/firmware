/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mon/set.h"
#include "mon/str.h"
#include "sys/cfg.h"
#include "sys/lfs.h"

#if defined(DEBUG_RIA_MON) || defined(DEBUG_RIA_MON_SET)
#include <stdio.h>
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
static inline void DBG(const char *fmt, ...)
{
    (void)fmt;
}
#endif

static void set_print_boot(void)
{
    const char *rom = cfg_get_boot();
    if (!rom[0])
        rom = "(none)";
    printf("BOOT: %s\n", rom);
}

static void set_boot(const char *args, size_t len)
{
    if (len)
    {
        char lfs_name[LFS_NAME_MAX + 1];
        if (args[0] == '-' && str_parse_end(++args, --len))
        {
            cfg_set_boot("");
        }
        else if (str_parse_rom_name(&args, &len, lfs_name)
                 && str_parse_end(args, len))
        {
            struct lfs_info info;
            if (lfs_stat(&lfs_volume, lfs_name, &info) < 0)
            {
                printf("?ROM not installed\n");
                return;
            }
            cfg_set_boot(lfs_name);
        }
        else
        {
            printf("?Invalid ROM name\n");
            return;
        }
    }
    set_print_boot();
}

static void set_print_code_page()
{
#if (RP6502_CODE_PAGE)
    printf("CP  : %d (dev)\n", RP6502_CODE_PAGE);
#else
    printf("CP  : %d\n", cfg_get_code_page());
#endif
}

static void set_code_page(const char *args, size_t len)
{
    uint32_t val;
    if (len)
    {
        if (!str_parse_uint32(&args, &len, &val)
            || !str_parse_end(args, len)
            || !cfg_set_code_page(val))
        {
            printf("?invalid argument\n");
            return;
        }
    }
    set_print_code_page();
}

static void set_print_time_zone(void)
{
    printf("TZ  : %s\n", cfg_get_time_zone());
}

static void set_time_zone(const char *args, size_t len)
{
    char tz[65];
    if (len)
    {
        if (!str_parse_string(&args, &len, tz, sizeof(tz))
            || !str_parse_end(args, len)
            || !cfg_set_time_zone(tz))
        {
            printf("?invalid argument\n");
            return;
        }
    }
    set_print_time_zone();
}

typedef void (*set_function)(const char *, size_t);
static struct
{
    size_t attr_len;
    const char *const attr;
    set_function func;
} const SETTERS[] = {
    {4, "boot", set_boot},
    {2, "tz", set_time_zone},
    {2, "cp", set_code_page},
};
static const size_t SETTERS_COUNT = sizeof SETTERS / sizeof *SETTERS;

static void set_print_all(void)
{
    set_print_boot();
    set_print_time_zone();
    set_print_code_page();
}

void set_mon_set(const char *args, size_t len)
{
    if (!len)
        return set_print_all();

    size_t i = 0;
    for (; i < len; i++)
        if (args[i] == ' ')
            break;
    size_t attr_len = i;
    for (; i < len; i++)
        if (args[i] != ' ')
            break;
    size_t args_start = i;
    for (i = 0; i < SETTERS_COUNT; i++)
    {
        if (attr_len == SETTERS[i].attr_len
            && !strncasecmp(args, SETTERS[i].attr, attr_len))
        {
            SETTERS[i].func(&args[args_start], len - args_start);
            return;
        }
    }
    printf("?Unknown attribute\n");
}
