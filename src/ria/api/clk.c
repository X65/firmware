/*
 * Copyright (c) 2023 Brentward
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "api/clk.h"
#include "api/api.h"
#include "fatfs/ff.h"
#include "hardware/timer.h"
#include "pico/aon_timer.h"
#include <time.h>

#define CLK_ID_REALTIME 0
#define CLK_EPOCH_UNIX  1970
#define CLK_EPOCH_FAT   1980

uint64_t clk_clock_start;

void clk_init(void)
{
    const struct timespec ts = {0};
    aon_timer_start(&ts);
}

void clk_run(void)
{
    clk_clock_start = time_us_64();
}

DWORD get_fattime(void)
{
    DWORD res;
    struct timespec ts = {0};
    aon_timer_get_time(&ts);
    struct tm *time = localtime(&ts.tv_sec);
    if (time && (time->tm_year >= CLK_EPOCH_FAT))
    {
        res = (((DWORD)time->tm_year - CLK_EPOCH_FAT) << 25) | //
              ((DWORD)time->tm_mon << 21) |                    //
              ((DWORD)time->tm_mday << 16) |                   //
              (WORD)(time->tm_hour << 11) |                    //
              (WORD)(time->tm_min << 5) |                      //
              (WORD)(time->tm_sec >> 1);
    }
    else
    {
        res = ((DWORD)(0) << 25 | (DWORD)1 << 21 | (DWORD)1 << 16);
    }
    return res;
}

void clk_api_clock(void)
{
    return api_return_axsreg((time_us_64() - clk_clock_start) / 10000);
}

void clk_api_get_res(void)
{
    uint8_t clock_id = API_A;
    if (clock_id == CLK_ID_REALTIME)
    {
        struct timespec ts;
        aon_timer_get_resolution(&ts);
        if (!api_push_int32((uint32_t *)&(ts.tv_nsec))
            || !api_push_uint32((uint32_t *)&(ts.tv_sec)))
            return api_return_errno(API_EINVAL);
        api_sync_xstack();
        return api_return_ax(0);
    }
    else
        return api_return_errno(API_EINVAL);
}

void clk_api_get_time(void)
{
    uint8_t clock_id = API_A;
    if (clock_id == CLK_ID_REALTIME)
    {
        struct timespec ts;
        aon_timer_get_time(&ts);
        if (!api_push_int32((uint32_t *)&(ts.tv_nsec))
            || !api_push_uint32((uint32_t *)&(ts.tv_sec)))
            return api_return_errno(API_EINVAL);
        api_sync_xstack();
        return api_return_ax(0);
    }
    else
        return api_return_errno(API_EINVAL);
}

void clk_api_set_time(void)
{
    uint8_t clock_id = API_A;
    if (clock_id == CLK_ID_REALTIME)
    {
        struct timespec ts;
        if (!api_pop_uint32((uint32_t *)&(ts.tv_sec))
            || !api_pop_int32_end((uint32_t *)&(ts.tv_nsec)))
            return api_return_errno(API_EINVAL);
        aon_timer_set_time(&ts);
        return api_return_ax(0);
    }
    else
        return api_return_errno(API_EINVAL);
}
