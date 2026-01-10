/*
 * Copyright (c) 2025 Rumbledethumps
 * Copyright (c) 2024-2026 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sys/vpu.h"
#include "sys/pix.h"
#include "sys/rln.h"
#include <hardware/clocks.h>
#include <pico/stdlib.h>
#include <stdio.h>

#if defined(DEBUG_RIA_SYS) || defined(DEBUG_RIA_SYS_VGA)
#include <stdio.h>
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
static inline void DBG(const char *fmt, ...)
{
    (void)fmt;
}
#endif

// How long to wait before aborting version string
#define VPU_VERSION_WATCHDOG_MS 2

bool vpu_needs_reset = true;

uint16_t vpu_raster;

char vpu_version_message[VPU_VERSION_MESSAGE_SIZE];
size_t vpu_version_message_length;

static void vpu_get_version(void)
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
    vpu_get_version();

    // Reset CGIA
    pix_send_request(PIX_DEV_CMD, 1,
                     (uint8_t[]) {PIX_DEVICE_CMD(PIX_DEV_VPU, PIX_VPU_CMD_RESET)},
                     nullptr);
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
    vpu_needs_reset = true;
}

int vpu_boot_response(char *buf, size_t buf_size, int state)
{
    (void)state;
    if (!*vpu_version_message)
        return -1;
    snprintf(buf, buf_size, "%s\n", *vpu_version_message ? vpu_version_message : "VPU Not Found");
    return -1;
}

static bool vpu_status_done;

static void vpu_rln_status_callback(bool timeout, const char *buf, size_t length)
{
    (void)buf;
    (void)length;

    if (timeout)
        vpu_status_done = true;
    else // reload for next line
        rln_read_line(VPU_VERSION_WATCHDOG_MS, vpu_rln_status_callback,
                      80, 0);
}

int vpu_status_response(char *buf, size_t buf_size, int state)
{
    (void)buf;
    (void)buf_size;
    (void)state;

    if (!pix_connected())
        return -1;

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
        return -2;
    while (!vpu_status_done)
    {
        rln_task();
    }

    return -1;
}
