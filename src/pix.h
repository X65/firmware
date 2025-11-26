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

#endif /* _PIX_H_ */
