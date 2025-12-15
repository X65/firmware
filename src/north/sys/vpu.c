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

uint16_t vpu_raster;

char vpu_version_message[VPU_VERSION_MESSAGE_SIZE];
size_t vpu_version_message_length;

static void vpu_connect(void)
{
    // Load VPU version string
    pix_response_t resp = {0};
    vpu_version_message[0] = '\0';
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
            vpu_version_message[0] = '\0';
            return;
        }

        const char ch = (char)PIX_REPLY_PAYLOAD(resp.reply);
        vpu_version_message[idx++] = ch;
        if (ch == '\0')
            break;
    }
    vpu_version_message_length = idx;
}

void vpu_init(void)
{
    // Connect and establish backchannel
    vpu_connect();
}

void vpu_task(void)
{
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
}

void vpu_print_status(void)
{
    puts(*vpu_version_message ? vpu_version_message : "VPU Not Found");
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
    if (!pix_connected())
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
