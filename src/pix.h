/*
 * Copyright (c) 2025 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _PIX_H_
#define _PIX_H_

/* Pico Information eXchange bus.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PIX_BUS_PIO_SPEED_KHZ 10000U

/*
 * PIX protocol messages
 */

typedef enum pix
{
    PIX_PING = 0,
    PIX_PONG,
    PIX_STATUS,
    PIX_MEM_WRITE,
    PIX_DMA_REQ,
    PIX_DMA_WRITE,
    PIX_DEV_CMD,
    PIX_DEV_WRITE,
    PIX_DEV_READ,
} pix_msg_type_t;

#define PIX_MESSAGE(msg_type, msg_len) \
    (((msg_type & 0b111) << 5) | ((msg_len - 1) & 0b11111))

typedef enum pix_dev
{
    PIX_DEV_RIA = 0,
    PIX_DEV_VPU = 1,
} pix_dev_t;

#define PIX_DEVICE_CMD(device, cmd) \
    ((cmd & 0xF) | ((device & 0xF) << 4))

typedef enum pix_vpu_cmd
{
    PIX_VPU_CMD_GET_STATUS = 0,
    PIX_VPU_CMD_SET_MODE_VT,
    PIX_VPU_CMD_SET_MODE_CGIA,
} pix_vpu_cmd_t;

#endif /* _PIX_H_ */
