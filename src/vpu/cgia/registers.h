#include <stdint.h>

enum CGIA
{
    CGIA_GFX_BANK,
    CGIA_SPR_BANK,

    CGIA_DL_OFFL,
    CGIA_DL_OFFH,

    CGIA_SPR_OFFL,
    CGIA_SPR_OFFH,
};

#define CGIA_SPR_OFF CGIA_SPR_OFFL

static uint8_t cgia_registers[0x40] = {0};
