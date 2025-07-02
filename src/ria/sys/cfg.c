/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/cfg.h"
#include "api/clk.h"
#include "api/oem.h"
#include "str.h"
#include "sys/cpu.h"
#include "sys/lfs.h"
#include "sys/mem.h"
#include <ctype.h>

// Configuration is a plain ASCII file on the LFS. e.g.
// +V1         | Version - Must be first
// +P8000      | PHI2 (retired)
// +C0         | Caps
// +R0         | RESB (retired)
// +TUTC0      | Time Zone
// +S437       | Code Page
// BASIC       | Boot ROM - Must be last

#define CFG_VERSION 1
static const char filename[] = "CONFIG.SYS";

static uint8_t cfg_caps;
static uint32_t cfg_codepage;
static char cfg_time_zone[65];

// Optional string can replace boot string
static void cfg_save_with_boot_opt(char *opt_str)
{
    lfs_file_t lfs_file;
    LFS_FILE_CONFIG(lfs_file_config);
    int lfsresult = lfs_file_opencfg(&lfs_volume, &lfs_file, filename,
                                     LFS_O_RDWR | LFS_O_CREAT,
                                     &lfs_file_config);
    if (lfsresult < 0)
    {
        printf("?Unable to lfs_file_opencfg %s for writing (%d)\n", filename, lfsresult);
        return;
    }
    if (!opt_str)
    {
        opt_str = (char *)mbuf;
        // Fetch the boot string, ignore the rest
        while (lfs_gets((char *)mbuf, MBUF_SIZE, &lfs_volume, &lfs_file))
            if (mbuf[0] != '+')
                break;
        if (lfsresult >= 0)
            if ((lfsresult = lfs_file_rewind(&lfs_volume, &lfs_file)) < 0)
                printf("?Unable to lfs_file_rewind %s (%d)\n", filename, lfsresult);
    }

    if (lfsresult >= 0)
        if ((lfsresult = lfs_file_truncate(&lfs_volume, &lfs_file, 0)) < 0)
            printf("?Unable to lfs_file_truncate %s (%d)\n", filename, lfsresult);
    if (lfsresult >= 0)
    {
        lfsresult = lfs_printf(&lfs_volume, &lfs_file,
                               "+V%u\n"
                               "+C%u\n"
                               "+T%s\n"
                               "+S%u\n"
                               "%s",
                               CFG_VERSION,
                               cfg_caps,
                               cfg_time_zone,
                               cfg_codepage,
                               opt_str);
        if (lfsresult < 0)
            printf("?Unable to write %s contents (%d)\n", filename, lfsresult);
    }
    int lfscloseresult = lfs_file_close(&lfs_volume, &lfs_file);
    if (lfscloseresult < 0)
        printf("?Unable to lfs_file_close %s (%d)\n", filename, lfscloseresult);
    if (lfsresult < 0 || lfscloseresult < 0)
        lfs_remove(&lfs_volume, filename);
}

static void cfg_load_with_boot_opt(bool boot_only)
{
    lfs_file_t lfs_file;
    LFS_FILE_CONFIG(lfs_file_config);
    int lfsresult = lfs_file_opencfg(&lfs_volume, &lfs_file, filename,
                                     LFS_O_RDONLY, &lfs_file_config);
    mbuf[0] = 0;
    if (lfsresult < 0)
    {
        if (lfsresult != LFS_ERR_NOENT)
            printf("?Unable to lfs_file_opencfg %s for reading (%d)\n", filename, lfsresult);
        return;
    }
    while (lfs_gets((char *)mbuf, MBUF_SIZE, &lfs_volume, &lfs_file))
    {
        if (mbuf[0] != '+')
            break;
        if (boot_only)
            continue;
        size_t len = strlen((char *)mbuf);
        while (len && mbuf[len - 1] == '\n')
            len--;
        mbuf[len] = 0;
        const char *str = (char *)mbuf + 2;
        len -= 2;
        switch (mbuf[1])
        {
        case 'C':
            parse_uint8(&str, &len, &cfg_caps);
            break;
        case 'T':
            parse_string(&str, &len, cfg_time_zone, sizeof(cfg_time_zone));
            break;
        case 'S':
            parse_uint32(&str, &len, &cfg_codepage);
            break;
        default:
            break;
        }
    }
    lfsresult = lfs_file_close(&lfs_volume, &lfs_file);
    if (lfsresult < 0)
        printf("?Unable to lfs_file_close %s (%d)\n", filename, lfsresult);
}

void cfg_init(void)
{
    cfg_load_with_boot_opt(false);
}

void cfg_set_boot(char *str)
{
    cfg_save_with_boot_opt(str);
}

char *cfg_get_boot(void)
{
    cfg_load_with_boot_opt(true);
    return (char *)mbuf;
}

void cfg_set_caps(uint8_t mode)
{
    if (mode <= 2 && cfg_caps != mode)
    {
        cfg_caps = mode;
        cfg_save_with_boot_opt(NULL);
    }
}

uint8_t cfg_get_caps(void)
{
    return cfg_caps;
}

bool cfg_set_time_zone(const char *tz)
{
    if (strlen(tz) < sizeof(cfg_time_zone) - 1)
    {
        const char *time_zone = clk_set_time_zone(tz);
        if (strcmp(cfg_time_zone, time_zone))
        {
            strcpy(cfg_time_zone, time_zone);
            cfg_save_with_boot_opt(NULL);
        }
        return true;
    }
    return false;
}

const char *cfg_get_time_zone(void)
{
    return cfg_time_zone;
}

bool cfg_set_codepage(uint32_t cp)
{
    if (cp > UINT16_MAX)
        return false;
    uint32_t old_val = cfg_codepage;
    cfg_codepage = oem_set_codepage(cp);
    if (old_val != cfg_codepage)
        cfg_save_with_boot_opt(NULL);
    return true;
}

uint16_t cfg_get_codepage(void)
{
    return cfg_codepage;
}
