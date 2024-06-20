/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _MAIN_H_
#define _MAIN_H_

#define DVI_DEFAULT_SERIAL_CONFIG picodvi_dvi_cfg

#define VPU_LED_PIN 7

#define VGA_PIX_PIO     pio1
#define VGA_PIX_REGS_SM 1
#define VGA_PIX_XRAM_SM 2
#define VGA_PHI2_PIN    8

void main_flush(void);
void main_reclock(void);

#endif /* _MAIN_H_ */
