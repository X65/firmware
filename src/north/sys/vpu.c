/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/vpu.h"
#include "../pix.h"
#include "sys/cpu.h"
#include "sys/mem.h"
#include "sys/pix.h"
#include "sys/ria.h"
#include <hardware/clocks.h>
#include <pico/stdlib.h>
#include <stdio.h>
#include <strings.h>

#if defined(DEBUG_RIA_SYS) || defined(DEBUG_RIA_SYS_VPU)
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
} vpu_state;

static absolute_time_t vpu_vsync_timer;
static absolute_time_t vpu_version_timer;

#define VPU_VERSION_MESSAGE_SIZE 80
char vpu_version_message[VPU_VERSION_MESSAGE_SIZE];
size_t vpu_version_message_length;

bool vpu_needs_reset;

static void vpu_backchannel_command(uint8_t byte)
{
    uint8_t scalar = byte & 0xF;
    switch (byte & 0xF0)
    {
    case 0x80:
        vpu_vsync_timer = make_timeout_time_ms(VPU_VSYNC_WATCHDOG_MS);
        static uint8_t vframe;
        if (scalar < (vframe & 0xF))
            vframe = (vframe & 0xF0) + 0x10;
        vframe = (vframe & 0xF0) | scalar;
        // REGS(0xFFE3) = vframe;
        ria_trigger_irq();
        break;
    case 0x90:
        // pix_ack();
        break;
    case 0xA0:
        // pix_nak();
        break;
    }
}

static void vpu_rln_callback(bool timeout, const char *buf, size_t length)
{
    // VGA1 means VPU on PIX 1
    if (!timeout && length == 4 && !strncasecmp("VGA1", buf, 4))
        vpu_state = VPU_FOUND;
    else
        vpu_state = VPU_NOT_FOUND;
}

static void vpu_connect(void)
{
    // // Test if VPU connected
    // uint8_t vpu_test_buf[4];
    // while (stdio_getchar_timeout_us(0) != PICO_ERROR_TIMEOUT)
    //     tight_loop_contents();
    // rln_read_binary(VPU_BACKCHANNEL_ACK_MS, vpu_rln_callback, vpu_test_buf, sizeof(vpu_test_buf));
    // vpu_pix_backchannel_request();
    // vpu_state = VPU_TESTING;
    // while (vpu_state == VPU_TESTING)
    //     rln_task();
    // if (vpu_state == VPU_NOT_FOUND)
    //     return vpu_pix_backchannel_disable();

    // // Turn on the backchannel
    // pio_gpio_init(VPU_BACKCHANNEL_PIO, VPU_BACKCHANNEL_PIN);
    // vpu_pix_backchannel_enable();

    // // Wait for version
    // vpu_version_message_length = 0;
    // vpu_version_timer = make_timeout_time_ms(VPU_VERSION_WATCHDOG_MS);
    // while (true)
    // {
    //     if (!pio_sm_is_rx_fifo_empty(VPU_BACKCHANNEL_PIO, VPU_BACKCHANNEL_SM))
    //     {
    //         uint8_t byte = pio_sm_get(VPU_BACKCHANNEL_PIO, VPU_BACKCHANNEL_SM) >> 24;
    //         if (!(byte & 0x80))
    //         {
    //             vpu_version_timer = make_timeout_time_ms(VPU_VERSION_WATCHDOG_MS);
    //             if (byte == '\r' || byte == '\n')
    //             {
    //                 if (vpu_version_message_length > 0)
    //                 {
    //                     vpu_vsync_timer = make_timeout_time_ms(VPU_VSYNC_WATCHDOG_MS);
    //                     vpu_state = VPU_CONNECTED;
    //                     break;
    //                 }
    //             }
    //             else if (vpu_version_message_length < VPU_VERSION_MESSAGE_SIZE - 1u)
    //             {
    //                 vpu_version_message[vpu_version_message_length] = byte;
    //                 vpu_version_message[++vpu_version_message_length] = 0;
    //             }
    //         }
    //     }
    //     if (absolute_time_diff_us(get_absolute_time(), vpu_version_timer) < 0)
    //     {
    //         vpu_vsync_timer = make_timeout_time_ms(VPU_VSYNC_WATCHDOG_MS);
    //         vpu_state = VPU_NO_VERSION;
    //         break;
    //     }
    // }
}

void vpu_init(void)
{
    // Reset Pico VPU
    vpu_needs_reset = true;

    // Connect and establish backchannel
    vpu_connect();
}

void vpu_task(void)
{
    if ((vpu_state == VPU_CONNECTED || vpu_state == VPU_NO_VERSION) && absolute_time_diff_us(get_absolute_time(), vpu_vsync_timer) < 0)
    {
        vpu_state = VPU_LOST_SIGNAL;
        printf("?");
        vpu_print_status();
    }

    if (vpu_needs_reset)
    {
        vpu_needs_reset = false;
        // pix_send_blocking(PIX_DEVICE_VPU, 0xF, 0x00, 0);
    }
}

void vpu_run(void)
{
    // Switch to CGIA mode
    pix_send_blocking(PIX_MESSAGE(PIX_DEV_CMD, 1));
    pix_send_blocking(PIX_DEVICE_CMD(PIX_DEV_VPU, PIX_VPU_CMD_SET_MODE_CGIA));
}

void vpu_stop(void)
{
    // Switch to VT mode
    pix_send_blocking(PIX_MESSAGE(PIX_DEV_CMD, 1));
    pix_send_blocking(PIX_DEVICE_CMD(PIX_DEV_VPU, PIX_VPU_CMD_SET_MODE_VT));
}

void vpu_break(void)
{
    vpu_needs_reset = true;
}

bool vpu_set_vPU(uint32_t display_type)
{
    // pix_send_blocking(PIX_DEVICE_VPU, 0xF, 0x00, display_type);
    return true;
}

bool vpu_connected(void)
{
    return vpu_state == VPU_CONNECTED
           || vpu_state == VPU_NO_VERSION;
}

void vpu_print_status(void)
{
    const char *msg = "VPU Searching";
    switch (vpu_state)
    {
    case VPU_FOUND:
    case VPU_TESTING:
        break;
    case VPU_CONNECTED:
        msg = vpu_version_message;
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
