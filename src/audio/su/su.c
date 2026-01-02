/* su.c/su.h - Sound Unit emulator
 * Copyright (C) 2015-2023 tildearrow
 * Copyright (C) 2025 smokku
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "su.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <string.h>

#define minval(a, b) (((a) < (b)) ? (a) : (b))
#define maxval(a, b) (((a) > (b)) ? (a) : (b))

#define FILVOL chan[4].special1C
#define ILCTRL chan[4].special1D
#define ILSIZE chan[5].special1C
#define FIL1   chan[5].special1D
#define IL1    chan[6].special1C
#define IL2    chan[6].special1D
#define IL0    chan[7].special1C
#define MVOL   chan[7].special1D

void SoundUnit_NextSample(SoundUnit *su, int16_t *l, int16_t *r)
{
    // run channels
    for (size_t i = 0; i < 8; i++)
    {
        if (su->chan[i].vol == 0 && !(su->chan[i].flags1 & 32))
        {
            su->fns[i] = 0;
            continue;
        }
        if (su->chan[i].flags0 & 8)
        {
            su->ns[i] = su->pcm[su->chan[i].pcmpos];
        }
        else
            switch (su->chan[i].flags0 & 7)
            {
            case 0:
                su->ns[i] = (((su->cycle[i] >> 15) & 127) > su->chan[i].duty) * 127;
                break;
            case 1:
                su->ns[i] = su->cycle[i] >> 14;
                break;
            case 2:
                su->ns[i] = su->SCsine[(su->cycle[i] >> 14) & 255];
                break;
            case 3:
                su->ns[i] = su->SCtriangle[(su->cycle[i] >> 14) & 255];
                break;
            case 4:
            case 5:
                su->ns[i] = (su->lfsr[i] & 1) * 127;
                break;
            case 6:
                su->ns[i] = ((((su->cycle[i] >> 15) & 127) > su->chan[i].duty) * 127)
                            ^ (short)su->SCsine[(su->cycle[i] >> 14) & 255];
                break;
            case 7:
                su->ns[i] = ((((su->cycle[i] >> 15) & 127) > su->chan[i].duty) * 127)
                            ^ (short)su->SCtriangle[(su->cycle[i] >> 14) & 255];
                break;
            }

        // ring mod
        if (su->chan[i].flags0 & 16)
        {
            su->ns[i] = (su->ns[i] * su->ns[(i + 1) & 7]) >> 7;
        }

        // PCM
        if (su->chan[i].flags0 & 8)
        {
            if (su->chan[i].freq > 0x8000)
            {
                su->pcmdec[i] += 0x8000;
            }
            else
            {
                su->pcmdec[i] += su->chan[i].freq;
            }
            if (su->pcmdec[i] >= 32768)
            {
                su->pcmdec[i] -= 32768;
                if (su->chan[i].pcmpos < su->chan[i].pcmbnd)
                {
                    su->chan[i].pcmpos++;
                    if (su->chan[i].pcmpos == su->chan[i].pcmbnd)
                    {
                        if (su->chan[i].flags1 & 4)
                        {
                            su->chan[i].pcmpos = su->chan[i].pcmrst;
                        }
                    }
                    su->chan[i].pcmpos &= (su->pcmSize - 1);
                }
                else if (su->chan[i].flags1 & 4)
                {
                    su->chan[i].pcmpos = su->chan[i].pcmrst;
                }
            }
        }
        else
        {
            su->ocycle[i] = su->cycle[i];
            if ((su->chan[i].flags0 & 7) == 5)
            {
                switch ((su->chan[i].duty >> 4) & 3)
                {
                case 0:
                    su->cycle[i] += su->chan[i].freq * 1 - (su->chan[i].freq >> 3);
                    break;
                case 1:
                    su->cycle[i] += su->chan[i].freq * 2 - (su->chan[i].freq >> 3);
                    break;
                case 2:
                    su->cycle[i] += su->chan[i].freq * 4 - (su->chan[i].freq >> 3);
                    break;
                case 3:
                    su->cycle[i] += su->chan[i].freq * 8 - (su->chan[i].freq >> 3);
                    break;
                }
            }
            else
            {
                su->cycle[i] += su->chan[i].freq;
            }
            if ((su->cycle[i] & 0xF80000) != (su->ocycle[i] & 0xF80000))
            {
                if ((su->chan[i].flags0 & 7) == 4)
                {
                    su->lfsr[i] = (su->lfsr[i] >> 1
                                   | (((su->lfsr[i]) ^ (su->lfsr[i] >> 2) ^ (su->lfsr[i] >> 3) ^ (su->lfsr[i] >> 5)) & 1) << 31);
                }
                else
                {
                    switch ((su->chan[i].duty >> 4) & 3)
                    {
                    case 0:
                        su->lfsr[i] = (su->lfsr[i] >> 1 | (((su->lfsr[i] >> 3) ^ (su->lfsr[i] >> 4)) & 1) << 5);
                        break;
                    case 1:
                        su->lfsr[i] = (su->lfsr[i] >> 1 | (((su->lfsr[i] >> 2) ^ (su->lfsr[i] >> 3)) & 1) << 5);
                        break;
                    case 2:
                        su->lfsr[i] = (su->lfsr[i] >> 1
                                       | (((su->lfsr[i]) ^ (su->lfsr[i] >> 2) ^ (su->lfsr[i] >> 3)) & 1) << 5);
                        break;
                    case 3:
                        su->lfsr[i] = (su->lfsr[i] >> 1
                                       | (((su->lfsr[i]) ^ (su->lfsr[i] >> 2) ^ (su->lfsr[i] >> 3) ^ (su->lfsr[i] >> 5)) & 1)
                                             << 5);
                        break;
                    }
                    if ((su->lfsr[i] & 63) == 0)
                    {
                        su->lfsr[i] = 0xAAAA;
                    }
                }
            }
            if (su->chan[i].flags1 & 8)
            {
                if (--su->rcycle[i] <= 0)
                {
                    su->cycle[i] = 0;
                    su->rcycle[i] = su->chan[i].restimer;
                    su->lfsr[i] = 0xAAAA;
                }
            }
        }
        su->fns[i] = su->ns[i] * su->chan[i].vol;
        if (!(su->chan[i].flags0 & 8))
            su->fns[i] >>= 1;
        if ((su->chan[i].flags0 & 0xE0) != 0)
        {
            int ff = su->chan[i].cutoff;
            su->nslow[i] = su->nslow[i] + (((ff)*su->nsband[i]) >> 16);
            su->nshigh[i] = su->fns[i] - su->nslow[i] - (((256 - su->chan[i].reson) * su->nsband[i]) >> 8);
            su->nsband[i] = (((ff)*su->nshigh[i]) >> 16) + su->nsband[i];
            su->fns[i] = (((su->chan[i].flags0 & 32) ? (su->nslow[i]) : (0))
                          + ((su->chan[i].flags0 & 64) ? (su->nshigh[i]) : (0))
                          + ((su->chan[i].flags0 & 128) ? (su->nsband[i]) : (0)));
        }
        su->nsL[i] = (su->fns[i] * su->SCpantabL[(uint8_t)su->chan[i].pan]) >> 8;
        su->nsR[i] = (su->fns[i] * su->SCpantabR[(uint8_t)su->chan[i].pan]) >> 8;
        su->oldfreq[i] = su->chan[i].freq;
        if (su->chan[i].flags1 & 32)
        {
            if (--su->swvolt[i] <= 0)
            {
                su->swvolt[i] = su->chan[i].swvol.speed;
                if (su->chan[i].swvol.amt & 32)
                {
                    su->chan[i].vol += su->chan[i].swvol.amt & 31;
                    if (su->chan[i].vol > su->chan[i].swvol.bound && !(su->chan[i].swvol.amt & 64))
                    {
                        su->chan[i].vol = su->chan[i].swvol.bound;
                    }
                    if (su->chan[i].vol & 0x80)
                    {
                        if (su->chan[i].swvol.amt & 64)
                        {
                            if (su->chan[i].swvol.amt & 128)
                            {
                                su->chan[i].swvol.amt ^= 32;
                                su->chan[i].vol = 0xFF - su->chan[i].vol;
                            }
                            else
                            {
                                su->chan[i].vol &= ~0x80;
                            }
                        }
                        else
                        {
                            su->chan[i].vol = 0x7F;
                        }
                    }
                }
                else
                {
                    su->chan[i].vol -= su->chan[i].swvol.amt & 31;
                    if (su->chan[i].vol & 0x80)
                    {
                        if (su->chan[i].swvol.amt & 64)
                        {
                            if (su->chan[i].swvol.amt & 128)
                            {
                                su->chan[i].swvol.amt ^= 32;
                                su->chan[i].vol = -su->chan[i].vol;
                            }
                            else
                            {
                                su->chan[i].vol &= ~0x80;
                            }
                        }
                        else
                        {
                            su->chan[i].vol = 0x00;
                        }
                    }
                    if (su->chan[i].vol < su->chan[i].swvol.bound && !(su->chan[i].swvol.amt & 64))
                    {
                        su->chan[i].vol = su->chan[i].swvol.bound;
                    }
                }
            }
        }
        if (su->chan[i].flags1 & 16)
        {
            if (--su->swfreqt[i] <= 0)
            {
                su->swfreqt[i] = su->chan[i].swfreq.speed;
                if (su->chan[i].swfreq.amt & 128)
                {
                    if (su->chan[i].freq > (0xFFFF - (su->chan[i].swfreq.amt & 127)))
                    {
                        su->chan[i].freq = 0xFFFF;
                    }
                    else
                    {
                        su->chan[i].freq = (su->chan[i].freq * (0x80 + (su->chan[i].swfreq.amt & 127))) >> 7;
                        if ((su->chan[i].freq >> 8) > su->chan[i].swfreq.bound)
                        {
                            su->chan[i].freq = su->chan[i].swfreq.bound << 8;
                        }
                    }
                }
                else
                {
                    if (su->chan[i].freq < (su->chan[i].swfreq.amt & 127))
                    {
                        su->chan[i].freq = 0;
                    }
                    else
                    {
                        su->chan[i].freq = (su->chan[i].freq * (0xFF - (su->chan[i].swfreq.amt & 127))) >> 8;
                        if ((su->chan[i].freq >> 8) < su->chan[i].swfreq.bound)
                        {
                            su->chan[i].freq = su->chan[i].swfreq.bound << 8;
                        }
                    }
                }
            }
        }
        if (su->chan[i].flags1 & 64)
        {
            if (--su->swcutt[i] <= 0)
            {
                su->swcutt[i] = su->chan[i].swcut.speed;
                if (su->chan[i].swcut.amt & 128)
                {
                    if (su->chan[i].cutoff > (0xFFFF - (su->chan[i].swcut.amt & 127)))
                    {
                        su->chan[i].cutoff = 0xFFFF;
                    }
                    else
                    {
                        su->chan[i].cutoff += su->chan[i].swcut.amt & 127;
                        if ((su->chan[i].cutoff >> 8) > su->chan[i].swcut.bound)
                        {
                            su->chan[i].cutoff = su->chan[i].swcut.bound << 8;
                        }
                    }
                }
                else
                {
                    if (su->chan[i].cutoff < (su->chan[i].swcut.amt & 127))
                    {
                        su->chan[i].cutoff = 0;
                    }
                    else
                    {
                        su->chan[i].cutoff = ((2048 - (unsigned int)(su->chan[i].swcut.amt & 127)) * (unsigned int)su->chan[i].cutoff)
                                             >> 11;
                        if ((su->chan[i].cutoff >> 8) < su->chan[i].swcut.bound)
                        {
                            su->chan[i].cutoff = su->chan[i].swcut.bound << 8;
                        }
                    }
                }
            }
        }
        if (su->chan[i].flags1 & 1)
        {
            su->cycle[i] = 0;
            su->rcycle[i] = su->chan[i].restimer;
            su->ocycle[i] = 0;
            su->chan[i].flags1 &= ~1;
        }
        if (su->muted[i])
        {
            su->nsL[i] = 0;
            su->nsR[i] = 0;
        }
    }

    // mix
    if (su->dsOut)
    {
        su->tnsL = su->nsL[su->dsChannel] << 3;
        su->tnsR = su->nsR[su->dsChannel] << 3;
        su->dsChannel = (su->dsChannel + 1) & 7;
    }
    else
    {
        su->tnsL = (su->nsL[0] + su->nsL[1] + su->nsL[2] + su->nsL[3] + su->nsL[4] + su->nsL[5] + su->nsL[6] + su->nsL[7]);
        su->tnsR = (su->nsR[0] + su->nsR[1] + su->nsR[2] + su->nsR[3] + su->nsR[4] + su->nsR[5] + su->nsR[6] + su->nsR[7]);

        su->IL1 = minval(32767, maxval(-32767, su->tnsL)) >> 8;
        su->IL2 = minval(32767, maxval(-32767, su->tnsR)) >> 8;
    }

    // write input lines to sample memory
    if (su->ILSIZE & 64)
    {
        if (++su->ilBufPeriod >= ((1 + (su->FIL1 >> 4)) << 2))
        {
            su->ilBufPeriod = 0;
            uint16_t ilLowerBound = su->pcmSize - ((1 + (su->ILSIZE & 63)) << 7);
            int16_t next;
            if (su->ilBufPos < ilLowerBound)
                su->ilBufPos = ilLowerBound;
            switch (su->ILCTRL & 3)
            {
            case 0:
                su->ilFeedback0 = su->ilFeedback1 = su->pcm[su->ilBufPos];
                next = ((int8_t)su->IL0) + ((su->pcm[su->ilBufPos] * (su->FIL1 & 15)) >> 4);
                if (next < -128)
                    next = -128;
                if (next > 127)
                    next = 127;
                su->pcm[su->ilBufPos] = next;
                if (++su->ilBufPos >= su->pcmSize)
                    su->ilBufPos = ilLowerBound;
                break;
            case 1:
                su->ilFeedback0 = su->ilFeedback1 = su->pcm[su->ilBufPos];
                next = ((int8_t)su->IL1) + ((su->pcm[su->ilBufPos] * (su->FIL1 & 15)) >> 4);
                if (next < -128)
                    next = -128;
                if (next > 127)
                    next = 127;
                su->pcm[su->ilBufPos] = next;
                if (++su->ilBufPos >= su->pcmSize)
                    su->ilBufPos = ilLowerBound;
                break;
            case 2:
                su->ilFeedback0 = su->ilFeedback1 = su->pcm[su->ilBufPos];
                next = ((int8_t)su->IL2) + ((su->pcm[su->ilBufPos] * (su->FIL1 & 15)) >> 4);
                if (next < -128)
                    next = -128;
                if (next > 127)
                    next = 127;
                su->pcm[su->ilBufPos] = next;
                if (++su->ilBufPos >= su->pcmSize)
                    su->ilBufPos = ilLowerBound;
                break;
            case 3:
                su->ilFeedback0 = su->pcm[su->ilBufPos];
                next = ((int8_t)su->IL1) + ((su->pcm[su->ilBufPos] * (su->FIL1 & 15)) >> 4);
                if (next < -128)
                    next = -128;
                if (next > 127)
                    next = 127;
                su->pcm[su->ilBufPos] = next;
                if (++su->ilBufPos >= su->pcmSize)
                    su->ilBufPos = ilLowerBound;
                su->ilFeedback1 = su->pcm[su->ilBufPos];
                next = ((int8_t)su->IL2) + ((su->pcm[su->ilBufPos] * (su->FIL1 & 15)) >> 4);
                if (next < -128)
                    next = -128;
                if (next > 127)
                    next = 127;
                su->pcm[su->ilBufPos] = next;
                if (++su->ilBufPos >= su->pcmSize)
                    su->ilBufPos = ilLowerBound;
                break;
            }
        }
        if (su->ILCTRL & 4)
        {
            if (su->ILSIZE & 128)
            {
                su->tnsL += su->ilFeedback1 * (int8_t)su->FILVOL;
                su->tnsR += su->ilFeedback0 * (int8_t)su->FILVOL;
            }
            else
            {
                su->tnsL += su->ilFeedback0 * (int8_t)su->FILVOL;
                su->tnsR += su->ilFeedback1 * (int8_t)su->FILVOL;
            }
        }
    }

    if (su->dsOut)
    {
        *l = minval(32767, maxval(-32767, su->tnsL)) & 0xFF00;
        *r = minval(32767, maxval(-32767, su->tnsR)) & 0xFF00;
    }
    else
    {
        *l = minval(32767, maxval(-32767, su->tnsL));
        *r = minval(32767, maxval(-32767, su->tnsR));
    }
}

void SoundUnit_Init(SoundUnit *su, size_t sampleMemSize, bool dsOutMode)
{
    su->pcmSize = sampleMemSize ? sampleMemSize : 8192;
    su->dsOut = dsOutMode;
    SoundUnit_Reset(su);
    memset(su->pcm, 0, su->pcmSize);
    for (size_t i = 0; i < 256; i++)
    {
        su->SCsine[i] = sin((i / 128.0f) * M_PI) * 127;
        su->SCtriangle[i] = (i > 127) ? (255 - i) : (i);
        su->SCpantabL[i] = 127;
        su->SCpantabR[i] = 127;
    }
    for (size_t i = 0; i < 128; i++)
    {
        su->SCpantabL[i] = 127 - i;
        su->SCpantabR[128 + i] = i - 1;
    }
    su->SCpantabR[128] = 0;
}

void SoundUnit_Reset(SoundUnit *su)
{
    for (size_t i = 0; i < 8; i++)
    {
        su->ocycle[i] = 0;
        su->cycle[i] = 0;
        su->rcycle[i] = 0;
        su->resetfreq[i] = 0;
        su->voldcycles[i] = 0;
        su->volicycles[i] = 0;
        su->fscycles[i] = 0;
        su->sweep[i] = 0;
        su->ns[i] = 0;
        su->fns[i] = 0;
        su->nsL[i] = 0;
        su->nsR[i] = 0;
        su->nslow[i] = 0;
        su->nshigh[i] = 0;
        su->nsband[i] = 0;
        su->swvolt[i] = 1;
        su->swfreqt[i] = 1;
        su->swcutt[i] = 1;
        su->lfsr[i] = 0xAAAA;
        su->oldfreq[i] = 0;
        su->pcmdec[i] = 0;
    }
    su->dsChannel = 0;
    su->tnsL = 0;
    su->tnsR = 0;
    su->ilBufPos = 0;
    su->ilBufPeriod = 0;
    su->ilFeedback0 = 0;
    su->ilFeedback1 = 0;
    memset(su->chan, 0, sizeof(struct SUChannel) * 8);
}

#ifdef TA_BIG_ENDIAN
static const uint8_t suBERemap[32] = {0x01, 0x00, 0x02, 0x03, 0x04, 0x05, 0x07, 0x06, 0x08, 0x09, 0x0B,
                                      0x0A, 0x0D, 0x0C, 0x0F, 0x0E, 0x11, 0x10, 0x12, 0x13, 0x15, 0x14,
                                      0x16, 0x17, 0x19, 0x18, 0x1A, 0x1B, 0x1C, 0x1D, 0x1F, 0x1E};
#endif

void SoundUnit_Write(SoundUnit *su, uint8_t addr, uint8_t data)
{
#ifdef TA_BIG_ENDIAN
    // remap
    addr = (addr & 0xE0) | (suBERemap[addr & 0x1F]);
#endif
    ((uint8_t *)su->chan)[addr] = data;
}
