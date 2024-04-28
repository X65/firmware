#include <stdint.h>
#include <stdio.h>

static const int8_t imbalance_lookup[16] = {-4, -2, -2, 0, -2, 0, 0, 2, -2, 0, 0, 2, 0, 2, 2, 4};

static inline int byte_imbalance(uint32_t x)
{
    return imbalance_lookup[x >> 4] + imbalance_lookup[x & 0xF];
}

static void tmds_encode_symbols(uint8_t pixel, uint32_t *negative_balance_sym, uint32_t *positive_balance_sym)
{
    int pixel_imbalance = byte_imbalance(pixel);
    uint32_t sym = pixel & 1;
    if (pixel_imbalance > 0 || (pixel_imbalance == 0 && sym == 0))
    {
        for (int i = 0; i < 7; ++i)
        {
            sym |= (~((sym >> i) ^ (pixel >> (i + 1))) & 1) << (i + 1);
        }
    }
    else
    {
        for (int i = 0; i < 7; ++i)
        {
            sym |= (((sym >> i) ^ (pixel >> (i + 1))) & 1) << (i + 1);
        }
        sym |= 0x100;
    }

    int imbalance = byte_imbalance(sym & 0xFF);
    if (imbalance == 0)
    {
        if ((sym & 0x100) == 0)
            sym ^= 0x2ff;
        *positive_balance_sym = sym;
        *negative_balance_sym = sym;
        return;
    }
    else if (imbalance > 0)
    {
        *negative_balance_sym = (sym ^ 0x2ff) | (((-imbalance + imbalance_lookup[2 ^ (sym >> 8)] + 2) & 0x3F) << 26);
        *positive_balance_sym = sym | ((imbalance + imbalance_lookup[sym >> 8] + 2) << 26);
    }
    else
    {
        *negative_balance_sym = sym | (((imbalance + imbalance_lookup[sym >> 8] + 2) & 0x3F) << 26);
        *positive_balance_sym = (sym ^ 0x2ff) | ((-imbalance + imbalance_lookup[2 ^ (sym >> 8)] + 2) << 26);
    }
}

int main()
{
    uint32_t negative_balance_sym;
    uint32_t positive_balance_sym;
    for (int i = 0; i < 128; ++i)
    {
        tmds_encode_symbols(i, &negative_balance_sym, &positive_balance_sym);
        printf("%3d: %08x %08x%s\n", i, negative_balance_sym, positive_balance_sym,
               negative_balance_sym == positive_balance_sym ? " =" : "");
    }
}
