/*
 * Copyright (c) 2025 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RIA_SYS_PIX_H_
#define _RIA_SYS_PIX_H_

/* Pico Information eXchange bus driver.
 */

#include "../pix.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Main events
 */

void pix_init(void);
void pix_task(void);
void pix_stop(void);

bool pix_connected(void);

// Structure to receive PIX responses into.
typedef struct
{
    volatile uint8_t status;
    volatile uint16_t reply;
} pix_response_t;

// Asynchronous PIX request.
// Reply will be inserted into resp when available.
void pix_send_request(pix_req_type_t msg_type,
                      uint8_t req_len5, uint8_t *req_data,
                      pix_response_t *resp);

// pass EVERY RAM write through CGIA for updating VRAM cache banks
void pix_mem_write(uint32_t addr24, uint8_t data);

#endif /* _RIA_SYS_PIX_H_ */
