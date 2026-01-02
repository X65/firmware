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

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    // private:
    int8_t SCsine[256];
    int8_t SCtriangle[256];
    int8_t SCpantabL[256];
    int8_t SCpantabR[256];
    uint32_t ocycle[8];
    uint32_t cycle[8];
    int32_t rcycle[8];
    uint32_t lfsr[8];
    int8_t ns[8];
    int16_t fns[8];
    int16_t nsL[8];
    int16_t nsR[8];
    int16_t nslow[8];
    int16_t nshigh[8];
    int16_t nsband[8];
    int32_t tnsL, tnsR;
    uint8_t ilBufPeriod;
    uint16_t ilBufPos;
    int8_t ilFeedback0;
    int8_t ilFeedback1;
    uint16_t oldfreq[8];
    uint32_t pcmSize;
    bool dsOut;
    uint8_t dsChannel;

    // public:
    uint16_t resetfreq[8];
    uint16_t voldcycles[8];
    uint16_t volicycles[8];
    uint16_t fscycles[8];
    uint8_t sweep[8];
    uint16_t swvolt[8];
    uint16_t swfreqt[8];
    uint16_t swcutt[8];
    uint16_t pcmdec[8];
    struct SUChannel
    {
        uint16_t freq;
        int8_t vol;
        int8_t pan;
        uint8_t flags0;
        uint8_t flags1;
        uint16_t cutoff;
        uint8_t duty;
        uint8_t reson;
        uint16_t pcmpos;
        uint16_t pcmbnd;
        uint16_t pcmrst;
        struct
        {
            uint16_t speed;
            uint8_t amt;
            uint8_t bound;
        } swfreq;
        struct
        {
            uint16_t speed;
            uint8_t amt;
            uint8_t bound;
        } swvol;
        struct
        {
            uint16_t speed;
            uint8_t amt;
            uint8_t bound;
        } swcut;
        uint8_t special1C;
        uint8_t special1D;
        uint16_t restimer;
    } chan[8];
    int8_t pcm[65536];
    bool muted[8];
} SoundUnit;

void SoundUnit_Init(SoundUnit *su, size_t sampleMemSize, bool dsOutMode);
void SoundUnit_Reset(SoundUnit *su);

void SoundUnit_Write(SoundUnit *su, uint8_t addr, uint8_t data);

void SoundUnit_NextSample(SoundUnit *su, int16_t *l, int16_t *r);
inline int32_t SoundUnit_GetSample(SoundUnit *su, int32_t ch)
{
    int32_t ret = (su->nsL[ch] + su->nsR[ch]) >> 1;
    if (ret < -32768)
        ret = -32768;
    if (ret > 32767)
        ret = 32767;
    return ret;
}
