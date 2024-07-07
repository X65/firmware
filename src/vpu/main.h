/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _MAIN_H_
#define _MAIN_H_

#define DVI_DEFAULT_SERIAL_CONFIG picodvi_dvi_cfg

#define VPU_LED_PIN 7

#define PIX_PIN_BASE 0
#define PIX_CLK_PIN  8

#define VPU_PIX_PIO     pio1
#define VPU_PIX_REGS_SM 2
#define VPU_PIX_XRAM_SM 3
#define VPU_PIX_SM      4

#define VPU_RAM_CTRL_BASE 19
#define VPU_RAM_DATA_BASE 22
#define VPU_RAM_PIO       pio1
#define VPU_RAM_CMD_SM    0
#define VPU_RAM_READ_SM   1

void main_flush(void);
void main_reclock(void);

#endif /* _MAIN_H_ */
