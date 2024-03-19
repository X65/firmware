/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _MAIN_H_
#define _MAIN_H_

#define DVI_DEFAULT_SERIAL_CONFIG x65_dvi_cfg
#define DVI_DEFAULT_PIO_INST      pio0
#define DVI_TMDS0_SM              0
#define DVI_TMDS1_SM              1
#define DVI_TMDS2_SM              2
#define DVI_TMDS0_PIN             2
#define DVI_TMDS1_PIN             8
#define DVI_TMDS2_PIN             6
#define DVI_CLK_PIN               0
#define DVI_DMA_IRQ               DMA_IRQ_0

#define VGA_PIX_PIO     pio1
#define VGA_PIX_REGS_SM 1
#define VGA_PIX_XRAM_SM 2
#define VGA_PHI2_PIN    11

void main_flush(void);
void main_reclock(void);

#endif /* _MAIN_H_ */
