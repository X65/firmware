/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RIA_SYS_MDM_H_
#define _RIA_SYS_MDM_H_

#include <stddef.h>
#include <stdint.h>

/* Kernel events
 */

void mdm_init(void);
void mdm_task(void);

/** API
 */
int32_t mdm_write_data_to_slave(const uint8_t *data, size_t size);

#endif /* _RIA_SYS_MDM_H_ */
