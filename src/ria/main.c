/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "main.h"
#include "api/clk.h"
#include "api/oem.h"
#include "cgia/cgia.h"
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
    out_init();
    com_init();
    ext_init(); // before aud_init (shared I2C init)

    // Print startup message
    sys_init();

    // Load config before we continue
    lfs_init();
    cfg_init();

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
    pad_task();
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
    bus_stop();
    aud_stop();
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

// Triggered once after init then after every PHI2 clock change.
// Divider is used when PHI2 less than 4 MHz to
// maintain a minimum system clock of 120 MHz.
// From 4 to 8 MHz increases system clock to 240 MHz.
void main_reclock(void)
{
    com_reclock();
    cpu_reclock();
    mem_reclock();
    out_reclock();
    ext_reclock();
    aud_reclock();
    mdm_reclock();
}

// This will repeatedly trigger until API_BUSY is false so
// IO operations can hold busy while waiting for data.
// Be sure any state is reset in a stop() handler.
bool main_api(uint8_t operation)
{
    switch (operation)
    {
    // case 0x01:
    //     pix_api_xreg();
    //     break;
    // case 0x02:
    //     cpu_api_phi2();
    //     break;
    case 0x03:
        oem_api_codepage();
        break;
    // case 0x04:
    //     rng_api_lrand();
    //     break;
    // case 0x05:
    //     cpu_api_stdin_opt();
    //     break;
    case 0x0F:
        clk_api_clock();
        break;
    case 0x10:
        clk_api_get_res();
        break;
    case 0x11:
        clk_api_get_time();
        break;
    case 0x12:
        clk_api_set_time();
        break;
    // case 0x14:
    //     std_api_open();
    //     break;
    // case 0x15:
    //     std_api_close();
    //     break;
    // case 0x16:
    //     std_api_read_xstack();
    //     break;
    // case 0x17:
    //     std_api_read_xram();
    //     break;
    // case 0x18:
    //     std_api_write_xstack();
    //     break;
    // case 0x19:
    //     std_api_write_xram();
    //     break;
    // case 0x1A:
    //     std_api_lseek();
    //     break;
    // case 0x1B:
    //     std_api_unlink();
    //     break;
    // case 0x1C:
    //     std_api_rename();
    //     break;
    default:
        return false;
    }
    return true;
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
