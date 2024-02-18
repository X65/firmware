/*
 * Copyright (c) 2023 Brentward
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "api/clk.h"
#include "fatfs/ff.h"
#include "hardware/rtc.h"
#include "hardware/timer.h"
#include "sys/cfg.h"
#include <time.h>

#define CLK_ID_REALTIME 0
#define CLK_EPOCH_UNIX  1970
#define CLK_EPOCH_FAT   1980

void clk_init(void)
{
    rtc_init();
    datetime_t rtc_info = {
        .year = CLK_EPOCH_UNIX,
        .month = 1,
        .day = 1,
        .dotw = 5,
        .hour = 0,
        .min = 0,
        .sec = 0,
    };
    rtc_set_datetime(&rtc_info);
}

DWORD get_fattime(void)
{
    DWORD res;
    datetime_t rtc_time;
    if (rtc_get_datetime(&rtc_time) && (rtc_time.year >= CLK_EPOCH_FAT))
    {
        res = (((DWORD)rtc_time.year - CLK_EPOCH_FAT) << 25) | //
              ((DWORD)rtc_time.month << 21) |                  //
              ((DWORD)rtc_time.day << 16) |                    //
              (WORD)(rtc_time.hour << 11) |                    //
              (WORD)(rtc_time.min << 5) |                      //
              (WORD)(rtc_time.sec >> 1);
    }
    else
    {
        res = ((DWORD)(0) << 25 | (DWORD)1 << 21 | (DWORD)1 << 16);
    }
    return res;
}
