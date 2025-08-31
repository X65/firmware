/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "api/oem.h"
#include "api/api.h"
#include "sys/cfg.h"
#include "term/font.h"
#include <fatfs/ff.h>

#if defined(DEBUG_RIA_API) || defined(DEBUG_RIA_API_OEM)
#include <stdio.h>
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
static inline void DBG(const char *fmt, ...)
{
    (void)fmt;
}
#endif

// Only the code page specified by RP6502_CODE_PAGE is installed to flash.
// To include all code pages, set RP6502_CODE_PAGE to 0 (CMmakeLists.txt).
// This is the default for when RP6502_CODE_PAGE == 0.
#define DEFAULT_CODE_PAGE 437

void oem_init(void)
{
    cfg_set_codepage(oem_set_codepage(cfg_get_codepage()));
}

static uint16_t oem_find_codepage(uint16_t cp)
{
#if RP6502_CODE_PAGE
    (void)cp;
    return RP6502_CODE_PAGE;
#else
    FRESULT result;
    if (cp)
    {
        result = f_setcp(cp);
        if (result == FR_OK)
            return cp;
    }
    uint16_t cfg_code_page = cfg_get_codepage();
    if (cfg_code_page)
    {
        result = f_setcp(cfg_code_page);
        if (result == FR_OK)
            return cfg_code_page;
    }
    f_setcp(DEFAULT_CODE_PAGE);
    return DEFAULT_CODE_PAGE;
#endif
}

uint16_t oem_set_codepage(uint16_t cp)
{
    cp = oem_find_codepage(cp);
    return cp;
}

bool oem_api_codepage(void)
{
    uint16_t cp;
    if (!api_pop_uint16_end(&cp))
        return api_return_errno(API_EINVAL);

    if (!cp)
        cp = cfg_get_codepage();
    return api_return_ax(oem_set_codepage(cp));
}

bool oem_api_get_chargen(void)
{
    uint16_t addr;
    if (!api_pop_uint16(&addr))
        return api_return_errno(API_EINVAL);
    uint8_t bank;
    if (!api_pop_uint8_end(&bank))
        return api_return_errno(API_EINVAL);

    mem_cpy_psram((bank << 16) | addr, font8, 256 * 8);

    return api_return_ax(0);
}

void oem_stop(void)
{
    oem_set_codepage(cfg_get_codepage());
}
