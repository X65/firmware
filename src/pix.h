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

#define PIX_BUS_PIO_SPEED_KHZ 42000U

/*
 * PIX protocol messages
 */

typedef enum
{
    PIX_SYNC = 0,
    PIX_PING,
    PIX_MEM_WRITE,
    PIX_DMA_WRITE,
    PIX_DEV_CMD,
    PIX_DEV_WRITE,
    PIX_DEV_READ,
} pix_req_type_t;

#define PIX_MESSAGE(req_type, req_len) \
    (uint8_t)(((req_type & 0b111) << 5) | ((req_len - 1) & 0b11111))

typedef enum
{
    PIX_ACK = 0,
    PIX_PONG,
    PIX_DMA_REQ,
    PIX_DEV_DATA,
    PIX_NAK = 0xFF,
} pix_rsp_code_t;

#define PIX_RESPONSE(rsp_code, rsp_payload) \
    (uint16_t)(((rsp_code & 0xF) << 12) | (rsp_payload & 0x0FFF))
#define PIX_REPLY_CODE(reply)    (((reply) >> 12) & 0x0F)
#define PIX_REPLY_PAYLOAD(reply) ((reply) & 0x0FFF)

typedef enum pix_dev
{
    PIX_DEV_RIA = 0,
    PIX_DEV_VPU = 1,
    PIX_DEV_SPU = 2,
} pix_dev_t;

#define PIX_DEVICE_CMD(device, cmd) \
    (uint8_t)((cmd & 0xF) | ((device & 0xF) << 4))

typedef enum pix_vpu_cmd
{
    PIX_VPU_CMD_GET_VERSION = 0,
    PIX_VPU_CMD_GET_STATUS,
    PIX_VPU_CMD_SET_MODE_VT,
    PIX_VPU_CMD_SET_MODE_CGIA,
    PIX_VPU_CMD_SET_CODE_PAGE,
} pix_vpu_cmd_t;

#define VPU_VERSION_MESSAGE_SIZE 20

#endif /* _PIX_H_ */
