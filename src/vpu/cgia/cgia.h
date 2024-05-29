#include "sys/types.h"

#define CGIA_COLORS_UNIQUE (16)
#define CGIA_COLORS_LEVELS (8)
#define CGIA_COLORS_NUM    (CGIA_COLORS_UNIQUE * CGIA_COLORS_LEVELS)

void cgia_init(void);
void cgia_core1_init(void);
void cgia_task(void);
void cgia_render(uint y, uint32_t *tmdsbuf);
