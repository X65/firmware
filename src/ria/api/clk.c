/*
 * Copyright (c) 2023 Brentward
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "api/clk.h"
#include "fatfs/ff.h"
#include "hardware/timer.h"
#include "pico.h"
#include "pico/aon_timer.h"
#include "sys/cfg.h"
#include <time.h>

#define CLK_ID_REALTIME 0
#define CLK_EPOCH_UNIX  1970
#define CLK_EPOCH_FAT   1980

void clk_init(void)
{
    const struct timespec ts = {0};
    aon_timer_start(&ts);
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
