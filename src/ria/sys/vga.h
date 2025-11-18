/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RIA_SYS_VGA_H_
#define _RIA_SYS_VGA_H_

/* Communications with RP6502-VGA.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Main events
 */

void vga_init(void);
void vga_task(void);
void vga_run(void);
void vga_stop(void);
void vga_break(void);

// Fully connected with backchannel.
bool vga_connected(void);

// For monitor status command.
void vga_print_status(void);

// Config handler.
bool vga_set_vga(uint32_t display_type);

#endif /* _RIA_SYS_VGA_H_ */
