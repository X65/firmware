/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _SND_USB_USB_H_
#define _SND_USB_USB_H_

/* USB device driver
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Main events
 */

void usb_init(void);
void usb_task(void);

/* Exported serial number for USB descriptor
 */

extern char serno[];

#endif /* _SND_USB_USB_H_ */
