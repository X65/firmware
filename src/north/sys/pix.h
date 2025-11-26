/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RIA_SYS_PIX_H_
#define _RIA_SYS_PIX_H_

/* Pico Information eXchange bus driver.
 */

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

// Unconditionally attempt to send a PIX message.
// Meant for use with pix_ready() to fill the FIFO in a task handler.
static inline void pix_send(uint8_t byte)
{
    pio_sm_put(PIX_PIO, PIX_SM, byte);
}

// Send a single PIX message, block if necessary. Normally, blocking is bad, but
// this unblocks so fast that it's not a problem for a few messages.
static inline void pix_send_blocking(uint8_t byte)
{
    while (!pix_ready())
        tight_loop_contents();
    pix_send(byte);
}

#endif /* _RIA_SYS_PIX_H_ */
