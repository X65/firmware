/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _SND_SYS_SPI_H_
#define _SND_SYS_SPI_H_

/* Host communication over SPI
 */

#include <stddef.h>
#include <stdint.h>

/* Main events
 */

void hst_init(void);
void hst_task(void);

#endif /* _SND_SYS_SPI_H_ */
