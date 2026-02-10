/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "api/api.h"
#include "api/oem.h"
#include "hid/kbd.h"
#include "mon/mon.h"
#include "mon/str.h"
#include "sys/cfg.h"
#include "sys/pix.h"
#include <fatfs/ff.h>

#if defined(DEBUG_RIA_API) || defined(DEBUG_RIA_API_OEM)
#include <stdio.h>
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
static inline void DBG(const char *fmt, ...) { (void)fmt; }
#endif

#define X(name, value) \
    static const char __in_flash(STRINGIFY(name)) name[] = value;

X(STR_ERR_INTERNAL_ERROR, "?Internal error\n")
#undef X

// Only the code page specified by RP6502_CODE_PAGE is installed to flash.
// To include all code pages, set RP6502_CODE_PAGE to 0 (CMmakeLists.txt).
// This is the default for when RP6502_CODE_PAGE == 0.
#define OEM_DEFAULT_CODE_PAGE 437

uint16_t oem_code_page_setting;
uint16_t oem_code_page;

static void oem_request_code_page(uint16_t cp)
{
    uint16_t old_code_page = oem_code_page;
#if RP6502_CODE_PAGE
    (void)cp;
    oem_code_page = RP6502_CODE_PAGE;
#else
    if (f_setcp(cp) == FR_OK)
        oem_code_page = cp;
    else if (oem_code_page == 0)
    {
        if (f_setcp(OEM_DEFAULT_CODE_PAGE) != FR_OK)
            mon_add_response_str(STR_ERR_INTERNAL_ERROR);
        oem_code_page = OEM_DEFAULT_CODE_PAGE;
    }
#endif
    if (old_code_page != oem_code_page)
    {
        pix_send_request(PIX_DEV_CMD, 3,
                         (uint8_t[]) {
                             PIX_DEVICE_CMD(PIX_DEV_VPU, PIX_VPU_CMD_SET_CODE_PAGE),
                             ((uint8_t *)&oem_code_page)[0],
                             ((uint8_t *)&oem_code_page)[1],
                         },
                         nullptr);
        kbd_rebuild_code_page_cache();
    }
}

void oem_init(void)
{
    if (!oem_code_page)
    {
        oem_request_code_page(OEM_DEFAULT_CODE_PAGE);
        oem_code_page_setting = oem_code_page;
    }
}

void oem_stop(void)
{
    if (oem_code_page != oem_code_page_setting)
        oem_request_code_page(oem_code_page_setting);
}

bool oem_api_code_page(void)
{
    oem_request_code_page(API_AX);
    return api_return_ax(oem_code_page);
}

bool oem_set_code_page(uint32_t cp)
{
    oem_request_code_page(cp);
    if (cp != oem_code_page)
        return false;
    if (oem_code_page_setting != oem_code_page)
    {
        oem_code_page_setting = oem_code_page;
        cfg_save();
    }
    return true;
}

uint16_t oem_get_code_page(void)
{
    return oem_code_page;
}

void oem_load_code_page(const char *str, size_t len)
{
    uint16_t cp;
    str_parse_uint16(&str, &len, &cp);
    oem_request_code_page(cp);
    oem_code_page_setting = oem_code_page;
}

static uint16_t chargen_cp;
static uint32_t chargen_addr;
static uint16_t pending_chargen_bytes = 0;
#define CHARGEN_TOTAL_BYTES (256 * 8)

bool oem_api_get_chargen(void)
{
    if (!pending_chargen_bytes)
    {
        uint16_t chargen_addr_low16;
        uint8_t chargen_addr_high8;

        if (!api_pop_uint16(&chargen_cp)
            || !api_pop_uint16(&chargen_addr_low16)
            || !api_pop_uint8(&chargen_addr_high8))
            return api_return_errno(API_EINVAL);

        chargen_addr = ((uint32_t)chargen_addr_high8 << 16) | chargen_addr_low16;
        pending_chargen_bytes = CHARGEN_TOTAL_BYTES;
    }

    if (pending_chargen_bytes)
    {
        const uint16_t chargen_byte = CHARGEN_TOTAL_BYTES - pending_chargen_bytes;
        pix_response_t resp = {0};
        pix_send_request(PIX_DEV_CMD, 5,
                         (uint8_t[]) {
                             PIX_DEVICE_CMD(PIX_DEV_VPU, PIX_VPU_CMD_GET_CHARGEN),
                             ((const uint8_t *)&chargen_byte)[0],
                             ((const uint8_t *)&chargen_byte)[1],
                             ((const uint8_t *)&chargen_cp)[0],
                             ((const uint8_t *)&chargen_cp)[1],
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
