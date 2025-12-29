/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _SND_SYS_COM_H_
#define _SND_SYS_COM_H_

/* Communications switchboard
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// OUT Buffer is a multiple of USB BULK_PACKET_SIZE.
// 1x will cause data loss on forwarded usb ports.
#define COM_OUT_BUF_SIZE (2 * 64)

/* Main events
 */

void com_init(void);

bool com_out_empty(void);
char com_out_peek(void);
char com_out_read(void);

#endif /* _SND_SYS_COM_H_ */
