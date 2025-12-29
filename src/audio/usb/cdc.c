/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "usb/cdc.h"
#include "sys/com.h"
#include <tusb.h>

void cdc_task(void)
{
    if (!tud_cdc_connected())
    {
        // flush stdout to null
        while (!com_out_empty())
            com_out_read();
    }
    else
    {
        if (!com_out_empty())
        {
            while (!com_out_empty() && tud_cdc_write_char(com_out_peek()))
                com_out_read();
            tud_cdc_write_flush();
        }
        if (tud_cdc_available())
        {
            tud_cdc_read_flush();
        }
    }
}
