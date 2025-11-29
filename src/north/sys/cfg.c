/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/cfg.h"
#include "api/clk.h"
#include "api/oem.h"
#include "mon/str.h"
#include "sys/cpu.h"
#include "sys/lfs.h"
#include "sys/mem.h"
#include "sys/vpu.h"
#include <ctype.h>

#if defined(DEBUG_RIA_SYS) || defined(DEBUG_RIA_SYS_CFG)
#include <stdio.h>
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
static inline void DBG(const char *fmt, ...)
{
    (void)fmt;
}
#endif

// Configuration is a plain ASCII file on the LFS. e.g.
// +V1         | Version - Must be first
// +P8000      | PHI2 (retired)
// +C0         | Caps (retired)
// +R0         | RESB (retired)
// +TUTC0      | Time Zone
// +S437       | Code Page
// BASIC       | Boot ROM - Must be last

#define CFG_VERSION 1
static const char cfg_filename[] = "CONFIG.SYS";

static uint32_t cfg_code_page;
static char cfg_time_zone[65];

// Optional string can replace boot string
static void cfg_save_with_boot_opt(char *opt_str)
{
    lfs_file_t lfs_file;
    LFS_FILE_CONFIG(lfs_file_config);
    int lfsresult = lfs_file_opencfg(&lfs_volume, &lfs_file, cfg_filename,
                                     LFS_O_RDWR | LFS_O_CREAT,
                                     &lfs_file_config);
    if (lfsresult < 0)
    {
        printf("?Unable to lfs_file_opencfg %s for writing (%d)\n", cfg_filename, lfsresult);
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
                printf("?Unable to lfs_file_rewind %s (%d)\n", cfg_filename, lfsresult);
    }

    if (lfsresult >= 0)
        if ((lfsresult = lfs_file_truncate(&lfs_volume, &lfs_file, 0)) < 0)
            printf("?Unable to lfs_file_truncate %s (%d)\n", cfg_filename, lfsresult);
    if (lfsresult >= 0)
    {
        lfsresult = lfs_printf(&lfs_volume, &lfs_file,
                               "+V%u\n"
                               "+T%s\n"
                               "+S%u\n"
                               "%s",
                               CFG_VERSION,
                               cfg_time_zone,
                               cfg_code_page,
                               opt_str);
        if (lfsresult < 0)
            printf("?Unable to write %s contents (%d)\n", cfg_filename, lfsresult);
    }
    int lfscloseresult = lfs_file_close(&lfs_volume, &lfs_file);
    if (lfscloseresult < 0)
        printf("?Unable to lfs_file_close %s (%d)\n", cfg_filename, lfscloseresult);
    if (lfsresult < 0 || lfscloseresult < 0)
        lfs_remove(&lfs_volume, cfg_filename);
}

static void cfg_load_with_boot_opt(bool boot_only)
{
    lfs_file_t lfs_file;
    LFS_FILE_CONFIG(lfs_file_config);
    int lfsresult = lfs_file_opencfg(&lfs_volume, &lfs_file, cfg_filename,
                                     LFS_O_RDONLY, &lfs_file_config);
    mbuf[0] = 0;
    if (lfsresult < 0)
    {
        if (lfsresult != LFS_ERR_NOENT)
            printf("?Unable to lfs_file_opencfg %s for reading (%d)\n", cfg_filename, lfsresult);
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
        case 'T':
            str_parse_string(&str, &len, cfg_time_zone, sizeof(cfg_time_zone));
            break;
        case 'S':
            str_parse_uint32(&str, &len, &cfg_code_page);
            break;
        default:
            break;
        }
    }
    lfsresult = lfs_file_close(&lfs_volume, &lfs_file);
    if (lfsresult < 0)
        printf("?Unable to lfs_file_close %s (%d)\n", cfg_filename, lfsresult);
}

void cfg_init(void)
{
    // Non 0 defaults
#ifdef RP6502_RIA_W
    cfg_net_rf = 1;
    cfg_net_ble = 1;
#endif /* RP6502_RIA_W */
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

bool cfg_set_code_page(uint32_t cp)
{
    if (cp > UINT16_MAX)
        return false;
    uint32_t old_val = cfg_code_page;
    cfg_code_page = oem_set_code_page(cp);
    if (old_val != cfg_code_page)
        cfg_save_with_boot_opt(NULL);
    return true;
}

uint16_t cfg_get_code_page(void)
{
    return cfg_code_page;
}
