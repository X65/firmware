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
#include "sys/rln.h"
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
    VPU_NOT_FOUND,   // VPU not yet checked
    VPU_TESTING,     // Looking for VPU
    VPU_FOUND,       // Found
    VPU_LOST_SIGNAL, // Definitely an error condition
} vpu_state;

static absolute_time_t vpu_raster_timer;
uint16_t vpu_raster;

bool vpu_needs_reset;

char vpu_version_message[VPU_VERSION_MESSAGE_SIZE];
size_t vpu_version_message_length;

static void vpu_connect(void)
{
    // Load VPU version string
    pix_response_t resp = {0};
    vpu_state = VPU_TESTING;
    uint8_t idx = 0;
    while (idx < VPU_VERSION_MESSAGE_SIZE)
    {
        resp.status = 0; // clear status for next round
        pix_send_request(PIX_DEV_CMD, 2,
                         (uint8_t[]) {PIX_DEVICE_CMD(PIX_DEV_VPU, PIX_VPU_CMD_GET_VERSION), idx},
                         &resp);
        while (!resp.status)
            tight_loop_contents();

        if (PIX_REPLY_CODE(resp.reply) != PIX_DEV_DATA)
        {
            vpu_state = VPU_NOT_FOUND;
            return;
        }

        vpu_state = VPU_FOUND;
        const char ch = (char)(PIX_REPLY_PAYLOAD(resp.reply));
        vpu_version_message[idx++] = ch;
        if (ch == '\0')
            break;
    }
    vpu_version_message_length = idx;
}

void vpu_init(void)
{
    // Reset VPU
    vpu_needs_reset = true;

    // Connect and establish backchannel
    vpu_connect();
}

void vpu_task(void)
{
    if (vpu_state == VPU_FOUND && absolute_time_diff_us(get_absolute_time(), vpu_raster_timer) < 0)
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
    pix_send_request(PIX_DEV_CMD, 1,
                     (uint8_t[]) {PIX_DEVICE_CMD(PIX_DEV_VPU, PIX_VPU_CMD_SET_MODE_CGIA)},
                     nullptr);
}

void vpu_stop(void)
{
    // Switch to VT mode
    pix_send_request(PIX_DEV_CMD, 1,
                     (uint8_t[]) {PIX_DEVICE_CMD(PIX_DEV_VPU, PIX_VPU_CMD_SET_MODE_VT)},
                     nullptr);
}

void vpu_break(void)
{
    vpu_needs_reset = true;
}

bool vpu_connected(void)
{
    return vpu_state == VPU_FOUND;
}

void vpu_print_status(void)
{
    const char *msg = "VPU Searching";
    switch (vpu_state)
    {
    case VPU_TESTING:
        break;
    case VPU_FOUND:
        msg = vpu_version_message;
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

static bool vpu_status_done;

static void vpu_rln_status_callback(bool timeout, const char *buf, size_t length)
{
    if (timeout)
        vpu_status_done = true;
    else // reload for next line
        rln_read_line(VPU_VERSION_WATCHDOG_MS, vpu_rln_status_callback,
                      80, 0);
}

void vpu_fetch_status(void)
{
    if (vpu_state != VPU_FOUND)
        return;

    while (stdio_getchar_timeout_us(0) != PICO_ERROR_TIMEOUT)
        tight_loop_contents();
    rln_read_line(VPU_VERSION_WATCHDOG_MS, vpu_rln_status_callback,
                  80, 0);
    vpu_status_done = false;
    pix_response_t resp = {0};
    uint8_t req = PIX_DEVICE_CMD(PIX_DEV_VPU, PIX_VPU_CMD_GET_STATUS);
    pix_send_request(PIX_DEV_CMD, 1, &req, &resp);
    while (!resp.status)
        tight_loop_contents();
    if (PIX_REPLY_CODE(resp.reply) != PIX_ACK)
        return;
    while (!vpu_status_done)
    {
        rln_task();
    }
}

void vpu_set_raster(uint16_t raster)
{
    vpu_raster = raster;
    vpu_raster_timer = make_timeout_time_ms(VPU_VSYNC_WATCHDOG_MS);
}
