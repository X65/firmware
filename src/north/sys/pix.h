/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RIA_SYS_PIX_H_
#define _RIA_SYS_PIX_H_

/* Pico Information eXchange bus driver.
 */

#include "../pix.h"
#include "hw.h"
#include <hardware/pio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Main events
 */

void pix_init(void);
void pix_task(void);
void pix_stop(void);

// /* API to set XREGs
//  */

// bool pix_api_xreg(void);
// void pix_ack(void);
// void pix_nak(void);

// // Well known PIX devices. 2-6 are for user expansion.
// // RIA device 0 is virtual, not on the physical PIX bus.

// #define PIX_DEVICE_XRAM 0
// #define PIX_DEVICE_RIA  0
// #define PIX_DEVICE_VGA  1
// #define PIX_DEVICE_IDLE 7

// // Bit 28 always 1, bits [31:29] for device id, etc.
// #define PIX_MESSAGE(dev, ch, byte, word) \
//     (0x10000000u | (dev << 29u) | (ch << 24) | ((byte) << 16) | (word))

// // Macro for the RIA. Use the inline functions elsewhere.
// #define PIX_SEND_XRAM(addr, data) \
//     PIX_PIO->txf[PIX_SM] = (PIX_MESSAGE(PIX_DEVICE_XRAM, 0, (data), (addr)))

// // Test for free space in the PIX transmit FIFO.
// static inline bool pix_ready(void)
// {
//     // PIX TX FIFO is joined to be 8 deep.
//     return pio_sm_get_tx_fifo_level(PIX_PIO, PIX_SM) < 6;
// }

static inline bool pix_ready(void)
{
    return !pio_sm_is_tx_fifo_full(PIX_PIO, PIX_SM);
}

// // Test for empty transmit FIFO.
// static inline bool pix_fifo_empty(void)
// {
//     return pio_sm_is_tx_fifo_empty(PIX_PIO, PIX_SM);
// }

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
