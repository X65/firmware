/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/vga.h"
#include "sys/cpu.h"
#include "sys/mem.h"
#include "sys/pix.h"
#include "sys/ria.h"
#include <hardware/clocks.h>
#include <pico/stdlib.h>
#include <stdio.h>
#include <strings.h>

#if defined(DEBUG_RIA_SYS) || defined(DEBUG_RIA_SYS_VGA)
#include <stdio.h>
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
static inline void DBG(const char *fmt, ...)
{
    (void)fmt;
}
#endif

// How long to wait for version string
#define VPU_VERSION_WATCHDOG_MS 2
// Abandon backchannel after two missed vsync messages (~2/60sec)
#define VPU_VSYNC_WATCHDOG_MS   35

static enum {
    VPU_NOT_FOUND,   // Possibly normal, Pico VPU is optional
    VPU_TESTING,     // Looking for Pico VPU
    VPU_FOUND,       // Found
    VPU_CONNECTED,   // Connected and version string received
    VPU_NO_VERSION,  // Connected but no version string received
    VPU_LOST_SIGNAL, // Definitely an error condition
} vga_state;

static absolute_time_t vga_vsync_timer;
static absolute_time_t vga_version_timer;

#define VPU_VERSION_MESSAGE_SIZE 80
char vga_version_message[VPU_VERSION_MESSAGE_SIZE];
size_t vga_version_message_length;

bool vga_needs_reset;

static void vga_backchannel_command(uint8_t byte)
{
    uint8_t scalar = byte & 0xF;
    switch (byte & 0xF0)
    {
    case 0x80:
        vga_vsync_timer = make_timeout_time_ms(VPU_VSYNC_WATCHDOG_MS);
        static uint8_t vframe;
        if (scalar < (vframe & 0xF))
            vframe = (vframe & 0xF0) + 0x10;
        vframe = (vframe & 0xF0) | scalar;
        REGS(0xFFE3) = vframe;
        ria_trigger_irq();
        break;
    case 0x90:
        pix_ack();
        break;
    case 0xA0:
        pix_nak();
        break;
    }
}

static void vga_rln_callback(bool timeout, const char *buf, size_t length)
{
    // VGA1 means VPU on PIX 1
    if (!timeout && length == 4 && !strncasecmp("VGA1", buf, 4))
        vga_state = VPU_FOUND;
    else
        vga_state = VPU_NOT_FOUND;
}

static void vga_connect(void)
{
    // // Test if VPU connected
    // uint8_t vga_test_buf[4];
    // while (stdio_getchar_timeout_us(0) != PICO_ERROR_TIMEOUT)
    //     tight_loop_contents();
    // rln_read_binary(VPU_BACKCHANNEL_ACK_MS, vga_rln_callback, vga_test_buf, sizeof(vga_test_buf));
    // vga_pix_backchannel_request();
    // vga_state = VPU_TESTING;
    // while (vga_state == VPU_TESTING)
    //     rln_task();
    // if (vga_state == VPU_NOT_FOUND)
    //     return vga_pix_backchannel_disable();

    // // Turn on the backchannel
    // pio_gpio_init(VPU_BACKCHANNEL_PIO, VPU_BACKCHANNEL_PIN);
    // vga_pix_backchannel_enable();

    // // Wait for version
    // vga_version_message_length = 0;
    // vga_version_timer = make_timeout_time_ms(VPU_VERSION_WATCHDOG_MS);
    // while (true)
    // {
    //     if (!pio_sm_is_rx_fifo_empty(VPU_BACKCHANNEL_PIO, VPU_BACKCHANNEL_SM))
    //     {
    //         uint8_t byte = pio_sm_get(VPU_BACKCHANNEL_PIO, VPU_BACKCHANNEL_SM) >> 24;
    //         if (!(byte & 0x80))
    //         {
    //             vga_version_timer = make_timeout_time_ms(VPU_VERSION_WATCHDOG_MS);
    //             if (byte == '\r' || byte == '\n')
    //             {
    //                 if (vga_version_message_length > 0)
    //                 {
    //                     vga_vsync_timer = make_timeout_time_ms(VPU_VSYNC_WATCHDOG_MS);
    //                     vga_state = VPU_CONNECTED;
    //                     break;
    //                 }
    //             }
    //             else if (vga_version_message_length < VPU_VERSION_MESSAGE_SIZE - 1u)
    //             {
    //                 vga_version_message[vga_version_message_length] = byte;
    //                 vga_version_message[++vga_version_message_length] = 0;
    //             }
    //         }
    //     }
    //     if (absolute_time_diff_us(get_absolute_time(), vga_version_timer) < 0)
    //     {
    //         vga_vsync_timer = make_timeout_time_ms(VPU_VSYNC_WATCHDOG_MS);
    //         vga_state = VPU_NO_VERSION;
    //         break;
    //     }
    // }
}

void vga_init(void)
{
    // Reset Pico VPU
    vga_needs_reset = true;

    // Connect and establish backchannel
    vga_connect();
}

void vga_task(void)
{
    if ((vga_state == VPU_CONNECTED || vga_state == VPU_NO_VERSION) && absolute_time_diff_us(get_absolute_time(), vga_vsync_timer) < 0)
    {
        vga_state = VPU_LOST_SIGNAL;
        printf("?");
        vga_print_status();
    }

    if (vga_needs_reset)
    {
        vga_needs_reset = false;
        pix_send_blocking(PIX_DEVICE_VGA, 0xF, 0x00, 0);
    }
}

void vga_run(void)
{
    // It's normal to lose signal during Pico VPU development.
    // Attempt to restart when a 6502 program is run.
    if (vga_state == VPU_LOST_SIGNAL && !cpu_active())
        vga_connect();
}

void vga_stop(void)
{
    // We want to reset only when program stops,
    // otherwise video flickers after every ria job.
    if (!cpu_active())
        vga_needs_reset = true;
}

void vga_break(void)
{
    vga_needs_reset = true;
}

bool vga_set_vga(uint32_t display_type)
{
    pix_send_blocking(PIX_DEVICE_VGA, 0xF, 0x00, display_type);
    return true;
}

bool vga_connected(void)
{
    return vga_state == VPU_CONNECTED
           || vga_state == VPU_NO_VERSION;
}

void vga_print_status(void)
{
    const char *msg = "VPU Searching";
    switch (vga_state)
    {
    case VPU_FOUND:
    case VPU_TESTING:
        break;
    case VPU_CONNECTED:
        msg = vga_version_message;
        break;
    case VPU_NO_VERSION:
        msg = "VPU Version Unknown";
        break;
    case VPU_NOT_FOUND:
        msg = "VPU Not Found";
        break;
    case VPU_LOST_SIGNAL:
        msg = "VPU Signal Lost";
        break;
    }
    puts(msg);
}
