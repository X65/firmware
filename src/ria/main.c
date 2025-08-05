/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "main.h"
#include "api/api.h"
#include "api/clk.h"
#include "api/oem.h"
#include "cgia/cgia.h"
#include "hardware/clocks.h"
#include "mon/fil.h"
#include "mon/mon.h"
#include "mon/rom.h"
#include "sys/aud.h"
#include "sys/bus.h"
#include "sys/cfg.h"
#include "sys/com.h"
#include "sys/cpu.h"
#include "sys/ext.h"
#include "sys/led.h"
#include "sys/lfs.h"
#include "sys/mdm.h"
#include "sys/mem.h"
#include "sys/out.h"
#include "sys/sys.h"
#include "term/font.h"
#include "term/term.h"
#include "usb/kbd.h"
#include "usb/mou.h"
#include "usb/pad.h"
#include "usb/xin.h"

/**************************************/
/* All kernel modules register below. */
/**************************************/

// Many things are sensitive to order in obvious ways, like
// starting the UART before printing. Please list subtleties.

// Initialization event for power up, reboot command, or reboot button.
static void init(void)
{
    // STDIO not available until after these inits

    mem_init();
    cpu_init();
    bus_init();
    font_init(); // before out_init (copies data from flash before overclocking)
    term_init();
    cgia_init();
    com_init();
    ext_init(); // before aud_init (shared I2C init)

    // Print startup message
    sys_init();

    // Load config before we continue
    lfs_init();
    cfg_init();

    // Print startup message after setting code page
    oem_init();
    sys_init();

    // Misc kernel modules, add yours here
    aud_init();
    kbd_init();
    mou_init();
    pad_init();
    mdm_init();
    rom_init();
    led_init();
    clk_init();

    // TinyUSB
    tuh_init(TUH_OPT_RHPORT);

    led_set_hartbeat(true);

    // finally start video output
    out_init();
}

// Tasks events are repeatedly called by the main kernel loop.
// They must not block. Use a state machine to do as
// much work as you can until something blocks.

// These tasks run when FatFs is blocking.
// Calling FatFs in here may cause undefined behavior.
void main_task(void)
{
    tuh_task();
    cpu_task();
    bus_task();
    term_task();
    cgia_task();
    aud_task();
    ext_task();
    mdm_task();
    kbd_task();
    xin_task();
    led_task();
}

// Tasks that call FatFs should be here instead of main_task().
static void task(void)
{
    com_task();
    mon_task();
    fil_task();
    rom_task();
}

// Event to start running the CPU.
static void run(void)
{
    clk_run();
    bus_run();
    cpu_run(); // Must be last
}

// Event to stop the CPU.
static void stop(void)
{
    cpu_stop(); // Must be first
    api_stop();
    bus_stop();
    aud_stop();
    oem_stop();
    kbd_stop();
    mou_stop();
    pad_stop();
}

// Event for CTRL-ALT-DEL and UART breaks.
static void reset(void)
{
    com_reset();
    fil_reset();
    mon_reset();
    rom_reset();
}

void main_pre_reclock()
{
    com_pre_reclock();
}

// Triggered once after init then after every PHI2 clock change.
void main_post_reclock()
{
    com_post_reclock();
    cpu_post_reclock();
    mem_post_reclock();
    out_post_reclock();
    ext_post_reclock();
    aud_post_reclock();
    mdm_post_reclock();
}

// API call implementations should return true if they have more
// work to process. They will be called repeatedly until returning
// false. Be sure any state is reset in a stop() handler.
bool main_api(uint8_t operation)
{
    switch (operation)
    {
    // case 0x01:
    //     return pix_api_xreg();
    //     break;
    // case 0x02:
    //     return cpu_api_phi2();
    //     break;
    case 0x03:
        return oem_api_codepage();
        break;
    // case 0x04:
    //     return rng_api_lrand();
    //     break;
    // case 0x05:
    //     return cpu_api_stdin_opt();
    //     break;
    case 0x0F:
        return clk_api_clock();
        break;
    case 0x10:
        return clk_api_get_res();
        break;
    case 0x11:
        return clk_api_get_time();
        break;
    case 0x12:
        return clk_api_set_time();
        break;
    case 0x13:
        return clk_api_get_time_zone();
        break;
        // case 0x14:
        //     return std_api_open();
        //     break;
        // case 0x15:
        //     return std_api_close();
        //     break;
        // case 0x16:
        //     return std_api_read_xstack();
        //     break;
        // case 0x17:
        //     return std_api_read_xram();
        //     break;
        // case 0x18:
        //     return std_api_write_xstack();
        //     break;
        // case 0x19:
        //     return std_api_write_xram();
        //     break;
        // case 0x1A:
        //     return std_api_lseek();
        //     break;
        // case 0x1B:
        //     return std_api_unlink();
        //     break;
        // case 0x1C:
        //     return std_api_rename();
        //     break;
    }
    api_return_errno(API_ENOSYS);
    return false;
}

/*********************************/
/* This is the kernel scheduler. */
/*********************************/

static bool is_breaking;
static enum state {
    stopped,
    starting,
    running,
    stopping,
} volatile main_state;

void main_run(void)
{
    if (main_state != running)
        main_state = starting;
}

void main_stop(void)
{
    if (main_state == starting)
        main_state = stopped;
    if (main_state != stopped)
        main_state = stopping;
}

void main_break(void)
{
    is_breaking = true;
}

bool main_active(void)
{
    return main_state != stopped;
}

int main(void)
{
    init();

    main_pre_reclock();

    // Reconfigure clocks, that the USB 48MHz clock is derived from system clock.
    // This requires that system clock is a multiple of 48 MHz. (no fractional divider)
    clock_configure(clk_usb,
                    0,
                    CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    SYS_CLK_HZ,
                    48 * MHZ);
    // Stop ADC clock (we do not use) which it is based off USB PLL.
    clock_stop(clk_adc);

    // Now we can use USB PLL to drive HSTX to achieve perfect 60Hz display refresh rate.

    main_post_reclock();

    while (true)
    {
        main_task();
        task();
        if (is_breaking)
        {
            if (main_state == starting)
                main_state = stopped;
            if (main_state == running)
                main_state = stopping;
        }
        if (main_state == starting)
        {
            // printf("starting\n");
            run();
            main_state = running;
        }
        if (main_state == stopping)
        {
            // printf("stopping\n");
            stop();
            main_state = stopped;
        }
        if (is_breaking)
        {
            reset();
            is_breaking = false;
        }
    }
    __builtin_unreachable();
}
