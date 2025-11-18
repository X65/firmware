/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Federico Zuccardi Merli
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "usb/usb.h"
#include <hardware/clocks.h>
#include <pico/unique_id.h>
#include <tusb.h>

/* C string for iSerialNumber in USB Device Descriptor, two chars per byte + terminating NUL */
char serno[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];

static void serno_init(void)
{
    pico_unique_board_id_t uID;
    pico_get_unique_board_id(&uID);

    // We use this Tiny
    for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2; i++)
    {
        /* Byte index inside the uid array */
        int bi = i / 2;
        /* Use high nibble first to keep memory order (just cosmetics) */
        uint8_t nibble = (uID.id[bi] >> 4) & 0x0F;
        uID.id[bi] <<= 4;
        /* Binary to hex digit */
        serno[i] = nibble < 10 ? nibble + '0' : nibble + 'A' - 10;
    }
}

void usb_init(void)
{
    // Reconfigure clocks, that the USB 48MHz clock is derived from system clock.
    // This requires that system clock is a multiple of 48 MHz. (no fractional divider)
    clock_configure(clk_usb,
                    0,
                    CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    SYS_CLK_HZ,
                    48 * MHZ);
    // Stop ADC clock (we do not use) which is based off USB PLL.
    clock_stop(clk_adc);

    serno_init(); // before tusb
    tusb_init();
}

void usb_task(void)
{
    tud_task();
}
