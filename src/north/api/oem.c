/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "api/oem.h"
#include "api/api.h"
#include "hid/kbd.h"
#include "sys/cfg.h"
#include "sys/pix.h"
#include <fatfs/ff.h>
#include <pico/stdlib.h>

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

uint16_t oem_code_page;

void oem_init(void)
{
    cfg_set_code_page(oem_set_code_page(cfg_get_code_page()));
}

static uint16_t oem_find_code_page(uint16_t cp)
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
    uint16_t cfg_code_page = cfg_get_code_page();
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

uint16_t oem_set_code_page(uint16_t cp)
{
    oem_code_page = oem_find_code_page(cp);
    pix_send_request(PIX_DEV_CMD, 3,
                     (uint8_t[]) {
                         PIX_DEVICE_CMD(PIX_DEV_VPU, PIX_VPU_CMD_SET_CODE_PAGE),
                         ((uint8_t *)&oem_code_page)[0],
                         ((uint8_t *)&oem_code_page)[1],
                     },
                     nullptr);
    kbd_rebuild_code_page_cache();
    return oem_code_page;
}

uint16_t oem_get_code_page(void)
{
    return oem_code_page;
}

bool oem_api_code_page(void)
{
    uint16_t cp = API_AX;
    if (!cp)
        cp = cfg_get_code_page();
    return api_return_ax(oem_set_code_page(cp));
}

static uint32_t chargen_addr;
static uint16_t pending_chargen_bytes = 0;

bool oem_api_get_chargen(void)
{
    if (!pending_chargen_bytes)
    {
        if (!api_pop_uint16(&((uint16_t *)(&chargen_addr))[0])
            || !api_pop_uint8(&((uint8_t *)(&chargen_addr))[2]))
            return api_return_errno(API_EINVAL);

        pending_chargen_bytes = 256 * 8;
    }

    if (pending_chargen_bytes)
    {
        pix_response_t resp = {0};
        pix_send_request(PIX_DEV_CMD, 3,
                         (uint8_t[]) {
                             PIX_DEVICE_CMD(PIX_DEV_VPU, PIX_VPU_CMD_GET_CHARGEN),
                             ((uint8_t *)&pending_chargen_bytes)[0],
                             ((uint8_t *)&pending_chargen_bytes)[1],
                         },
                         &resp);
        while (!resp.status)
            tight_loop_contents();

        if (PIX_REPLY_CODE(resp.reply) != PIX_DEV_DATA)
        {
            pending_chargen_bytes = 0;
            return api_return_errno(API_EIO);
        }

        mem_write_ram(chargen_addr++, (uint8_t)PIX_REPLY_PAYLOAD(resp.reply));
        --pending_chargen_bytes;
    }

    return pending_chargen_bytes ? api_working() : api_return();
}

void oem_stop(void)
{
    oem_set_code_page(cfg_get_code_page());
}
