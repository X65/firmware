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
#include <assert.h>
#include <pico.h>
#ifndef __not_in_flash_func
#define __not_in_flash_func(x) x
#endif

#define _USE_MATH_DEFINES
#include <math.h>
#include <string.h>

#define minval(a, b) (((a) < (b)) ? (a) : (b))
#define maxval(a, b) (((a) > (b)) ? (a) : (b))

// Short name used throughout: Pm is an internal multiplier used to scale all time-based steps
// (phase increment, sweep timers, restimer timers) to your chosen internal generation rate.
#define Pm (SOUND_UNIT_PHASE_MULTIPLIER)

// -----------------------------------------------------------------------------
// DC blocker: adaptive one-pole high-pass (implemented as tracking DC estimate).
// - dc_q8 holds the estimated DC in Q8 (x256) units.
// - Uses a faster shift when error is large (reduces audible "settling" after jumps),
//   and a slower shift otherwise (less bass loss / less modulation).
// -----------------------------------------------------------------------------
#define DC_SHIFT_FAST 9
#define DC_SHIFT_SLOW 12
#define DC_THRESH_Q8  (64 << 8) // threshold in Q8 units (tune)

static inline int32_t __attribute__((always_inline))
dc_block_q8(int32_t x, int32_t *dc_q8)
{
    int32_t x_q8 = x << 8;     // promote x to Q8
    int32_t e = x_q8 - *dc_q8; // error between signal and DC estimate
    int32_t ae = (e >= 0) ? e : -e;

    // If the signal jumps far from the current DC estimate, converge faster.
    int sh = (ae > DC_THRESH_Q8) ? DC_SHIFT_FAST : DC_SHIFT_SLOW;
    *dc_q8 += (e >> sh); // IIR integrator update of DC estimate

    return x - (*dc_q8 >> 8); // subtract estimated DC (back to integer units)
}

// -----------------------------------------------------------------------------
// Generate one stereo sample (l,r) for the whole 8-channel chip.
// Order per channel:
//   1) generate raw waveform or PCM sample into ns[i]
//   2) optional ring modulation (ns[i] *= ns[next])
//   3) advance phase / advance PCM position / advance noise LFSR
//   4) apply volume -> fns[i]
//   5) optional resonant SVF (LP/HP/BP selection)
//   6) DC block
//   7) pan -> nsL/nsR
//   8) apply sweeps (vol/freq/cutoff) for next samples
//   9) apply one-shot phase reset request
// Finally: sum all nsL/nsR into output.
// -----------------------------------------------------------------------------
void __not_in_flash_func(SoundUnit_NextSample)(SoundUnit *su, int32_t *l, int32_t *r)
{
    // Cache channel 0 raw sample from the previous iteration of the loop body.
    // Ringmod uses "next channel" sample; channel 7 uses channel 0, so caching makes it stable
    // and consistent even though ns[0] will be recomputed when i==0 in this loop.
    int8_t ns0 = su->src_sample_i8[0];

    for (size_t i = 0; i < 8; i++)
    {
        // ------------------------------------------------------------
        // 1) Raw source: PCM or oscillator waveform -> ns[i]
        // ------------------------------------------------------------
        if (su->chan[i].flags0 & 8) // bit3: PCM enable
        {
            // Signed 8-bit PCM sample at current position.
            su->src_sample_i8[i] = su->pcm[su->chan[i].pcmpos];
        }
        else
        {
            // bits0..2: waveform selector
            switch (su->chan[i].flags0 & 7)
            {
            case SGU1_FLAGS0_WAVE_PULSE:
                // Pulse: compare phase-derived 7-bit ramp against duty (0..127).
                // Output is unipolar (0 or 127).
                su->src_sample_i8[i] = (((su->phase_accum_prev[i] >> 15) & 127) > su->chan[i].duty) * 127;
                break;

            case SGU1_FLAGS0_WAVE_SAWTOOTH:
                // Saw: take top bits of phase ramp.
                // Note: su->cycle>>14 yields a value wider than int8; truncation wraps in int8.
                su->src_sample_i8[i] = su->phase_accum_prev[i] >> 14;
                break;

            case SGU1_FLAGS0_WAVE_SINE:
                // Sine: table lookup using 8-bit phase index.
                su->src_sample_i8[i] = su->wave_sine_lut[(su->phase_accum_prev[i] >> 14) & 255];
                break;

            case SGU1_FLAGS0_WAVE_TRIANGLE:
                // Triangle: table lookup.
                su->src_sample_i8[i] = su->wave_triangle_lut[(su->phase_accum_prev[i] >> 14) & 255];
                break;

            case SGU1_FLAGS0_WAVE_NOISE:
            case SGU1_FLAGS0_WAVE_PERIODIC_NOISE:
                // Noise outputs are unipolar (0 or 127) based on LFSR LSB.
                // (Periodic vs white noise differs in how LFSR is advanced below.)
                su->src_sample_i8[i] = (su->noise_lfsr[i] & 1) * 127;
                break;

            case SGU1_FLAGS0_WAVE_XOR_SINE:
                // XOR Sine: pulse XOR sine (hybrid timbre).
                su->src_sample_i8[i] = ((((su->phase_accum_prev[i] >> 15) & 127) > su->chan[i].duty) * 127)
                                       ^ (short)su->wave_sine_lut[(su->phase_accum_prev[i] >> 14) & 255];
                break;

            case SGU1_FLAGS0_WAVE_XOR_TRIANGLE:
                // XOR Triangle: pulse XOR triangle.
                su->src_sample_i8[i] = ((((su->phase_accum_prev[i] >> 15) & 127) > su->chan[i].duty) * 127)
                                       ^ (short)su->wave_triangle_lut[(su->phase_accum_prev[i] >> 14) & 255];
                break;
            }
        }

        // ------------------------------------------------------------
        // 2) Ring modulation (amplitude/ringmod style)
        // ------------------------------------------------------------
        if (su->chan[i].flags0 & 16) // bit4: ring mod enable
        {
            // Multiply current channel by "next channel" (i+1), or by cached ch0 for channel 7.
            // >>7 rescales back to roughly 8-bit range.
            su->src_sample_i8[i] = (su->src_sample_i8[i] * (i == 7 ? ns0 : su->src_sample_i8[(i + 1)])) >> 7;
        }

        // ------------------------------------------------------------
        // 3) Time advance: PCM position OR oscillator phase + noise stepping
        // ------------------------------------------------------------
        if (su->chan[i].flags0 & 8) // PCM mode
        {
            // pcmdec accumulates a fractional step. When it crosses 32768, advance pcmpos by 1.
            // Special case: if freq > 0x8000, treat as "at least 1.0 step" (clamps very high rates).
            if (su->chan[i].freq > 0x8000)
                su->pcm_phase_accum[i] += 0x8000;
            else
                su->pcm_phase_accum[i] += su->chan[i].freq * Pm;

            while (su->pcm_phase_accum[i] >= 32768)
            {
                su->pcm_phase_accum[i] -= 32768;

                // Advance sample pointer with boundary and optional looping.
                if (su->chan[i].pcmpos < su->chan[i].pcmbnd)
                {
                    su->chan[i].pcmpos++;

                    // If we hit the boundary exactly, loop if enabled.
                    if (su->chan[i].pcmpos == su->chan[i].pcmbnd)
                    {
                        if (su->chan[i].flags1 & 4) // bit2: PCM loop
                            su->chan[i].pcmpos = su->chan[i].pcmrst;
                    }

                    // Wrap to PCM RAM size (power-of-2 ring buffer).
                    su->chan[i].pcmpos &= (su->pcm_ram_size - 1);
                }
                else if (su->chan[i].flags1 & 4)
                {
                    // If already at/over boundary and looping, force restart.
                    su->chan[i].pcmpos = su->chan[i].pcmrst;
                }
            }
        }
        else // oscillator mode
        {
            su->phase_accum[i] = su->phase_accum_prev[i];

            // Periodic noise uses duty high bits to pick a frequency multiplier (pitching the noise).
            if ((su->chan[i].flags0 & 7) == SGU1_FLAGS0_WAVE_PERIODIC_NOISE)
            {
                switch ((su->chan[i].duty >> 4) & 3)
                {
                case 0:
                    su->phase_accum_prev[i] += (su->chan[i].freq * 1 - (su->chan[i].freq >> 3)) * Pm;
                    break;
                case 1:
                    su->phase_accum_prev[i] += (su->chan[i].freq * 2 - (su->chan[i].freq >> 3)) * Pm;
                    break;
                case 2:
                    su->phase_accum_prev[i] += (su->chan[i].freq * 4 - (su->chan[i].freq >> 3)) * Pm;
                    break;
                case 3:
                    su->phase_accum_prev[i] += (su->chan[i].freq * 8 - (su->chan[i].freq >> 3)) * Pm;
                    break;
                }
            }
            else
            {
                // Normal phase advance.
                su->phase_accum_prev[i] += su->chan[i].freq * Pm;
            }

            // Update the LFSR only when the phase crosses a coarse boundary.
            // This makes noise "rate" depend on frequency rather than advancing every sample.
            if ((su->phase_accum_prev[i] & 0xF80000) != (su->phase_accum[i] & 0xF80000))
            {
                if ((su->chan[i].flags0 & 7) == SGU1_FLAGS0_WAVE_NOISE)
                {
                    // 32-bit LFSR with taps (0,2,3,5) folded into MSB feedback.
                    su->noise_lfsr[i] = (su->noise_lfsr[i] >> 1) | ((((su->noise_lfsr[i]) ^ (su->noise_lfsr[i] >> 2) ^ (su->noise_lfsr[i] >> 3) ^ (su->noise_lfsr[i] >> 5)) & 1) << 31);
                }
                else
                {
                    // "Periodic noise": use a small LFSR (feedback bit inserted at bit 5).
                    // Different tap choices selected by duty high bits.
                    switch ((su->chan[i].duty >> 4) & 3)
                    {
                    case 0:
                        su->noise_lfsr[i] = (su->noise_lfsr[i] >> 1) | ((((su->noise_lfsr[i] >> 3) ^ (su->noise_lfsr[i] >> 4)) & 1) << 5);
                        break;
                    case 1:
                        su->noise_lfsr[i] = (su->noise_lfsr[i] >> 1) | ((((su->noise_lfsr[i] >> 2) ^ (su->noise_lfsr[i] >> 3)) & 1) << 5);
                        break;
                    case 2:
                        su->noise_lfsr[i] = (su->noise_lfsr[i] >> 1) | ((((su->noise_lfsr[i]) ^ (su->noise_lfsr[i] >> 2) ^ (su->noise_lfsr[i] >> 3)) & 1) << 5);
                        break;
                    case 3:
                        su->noise_lfsr[i] = (su->noise_lfsr[i] >> 1) | ((((su->noise_lfsr[i]) ^ (su->noise_lfsr[i] >> 2) ^ (su->noise_lfsr[i] >> 3) ^ (su->noise_lfsr[i] >> 5)) & 1) << 5);
                        break;
                    }

                    // Avoid getting stuck in an all-zero short cycle.
                    if ((su->noise_lfsr[i] & 63) == 0)
                        su->noise_lfsr[i] = 0xAAAA;
                }
            }

            // Periodic phase reset timer ("timer sync"):
            // If enabled and restimer != 0, rcycle counts down and resets phase+noise seed periodically.
            if ((su->chan[i].flags1 & 8) && su->chan[i].restimer) // bit3: TIMER_SYNC
            {
                su->phase_reset_countdown[i] -= Pm;
                while (su->phase_reset_countdown[i] <= 0)
                {
                    su->phase_reset_countdown[i] += su->chan[i].restimer;
                    su->phase_accum_prev[i] = 0; // reset oscillator phase
                    su->noise_lfsr[i] = 0xAAAA;  // reset noise seed
                }
            }
        }

        // ------------------------------------------------------------
        // 4) Volume scaling -> fns[i]
        // ------------------------------------------------------------
        su->voice_sample[i] = su->src_sample_i8[i] * su->chan[i].vol;

        // In oscillator mode only, halve level (extra headroom).
        if (!(su->chan[i].flags0 & 8))
            su->voice_sample[i] >>= 1;

        // ------------------------------------------------------------
        // 5) Optional resonant filter (state-variable filter)
        // flags0 bits 5..7 select which outputs to mix: LP/HP/BP.
        // ------------------------------------------------------------
        if ((su->chan[i].flags0 & 0xE0) != 0)
        {
            // ff is the cutoff coefficient scaled for the current internal rate (Pm).
            // Clamp for stability at higher generation rates.
            int ff = su->chan[i].cutoff * Pm;
            if (ff > 32768)
                ff = 32768;

            // SVF core (fixed-point-ish):
            su->svf_low[i] = su->svf_low[i] + (((ff)*su->svf_band[i]) >> 16);
            su->svf_high[i] = su->voice_sample[i] - su->svf_low[i] - (((256 - su->chan[i].reson) * su->svf_band[i]) >> 8);
            su->svf_band[i] = (((ff)*su->svf_high[i]) >> 16) + su->svf_band[i];

            // Select which components to output (LP/HP/BP can be combined).
            su->voice_sample[i] = ((su->chan[i].flags0 & 32) ? su->svf_low[i] : 0)
                                  + ((su->chan[i].flags0 & 64) ? su->svf_high[i] : 0)
                                  + ((su->chan[i].flags0 & 128) ? su->svf_band[i] : 0);
        }

        // ------------------------------------------------------------
        // 6) DC blocking (reduces clicks and removes DC from unipolar waveforms)
        // ------------------------------------------------------------
        su->voice_sample[i] = dc_block_q8(su->voice_sample[i], &su->dc_tracker_q8[i]);

        // ------------------------------------------------------------
        // 7) Panning (apply per-channel stereo gains)
        // ------------------------------------------------------------
        su->voice_left[i] = (su->voice_sample[i] * su->pan_gain_lut_l[(uint8_t)su->chan[i].pan]) >> 8;
        su->voice_right[i] = (su->voice_sample[i] * su->pan_gain_lut_r[(uint8_t)su->chan[i].pan]) >> 8;

        // ------------------------------------------------------------
        // 8) Sweeps (affect parameters for future samples)
        // ------------------------------------------------------------

        // 8a) Volume sweep (flags1 bit5).
        // swvol.amt encoding (as implemented here):
        //   bit5 (0x20): direction (1=up, 0=down)
        //   bits0..4: step size
        //   bit6 (0x40): "wrap/loop" behavior
        //   bit7 (0x80): "bounce/alternate" behavior
        if ((su->chan[i].flags1 & 32) && su->chan[i].swvol.speed)
        {
            su->vol_sweep_countdown[i] -= Pm;
            while (su->vol_sweep_countdown[i] <= 0)
            {
                su->vol_sweep_countdown[i] += su->chan[i].swvol.speed;

                if (su->chan[i].swvol.amt & 32) // up
                {
                    int v = su->chan[i].vol + (su->chan[i].swvol.amt & 31);
                    su->chan[i].vol = (v > 127)    ? 127
                                      : (v < -128) ? -128
                                                   : (int8_t)v;

                    // If not wrapping, clamp at upper bound.
                    if (su->chan[i].vol > su->chan[i].swvol.bound && !(su->chan[i].swvol.amt & 64))
                        su->chan[i].vol = su->chan[i].swvol.bound;

                    // Handle wrap/bounce on overflow sign bit.
                    if (su->chan[i].vol & 0x80)
                    {
                        if (su->chan[i].swvol.amt & 64) // wrap enabled
                        {
                            if (su->chan[i].swvol.amt & 128) // bounce enabled
                            {
                                su->chan[i].swvol.amt ^= 32;              // flip direction
                                su->chan[i].vol = 0xFF - su->chan[i].vol; // reflect
                            }
                            else
                            {
                                su->chan[i].vol &= ~0x80; // wrap into positive
                            }
                        }
                        else
                        {
                            su->chan[i].vol = 0x7F; // clamp
                        }
                    }
                }
                else // down
                {
                    int v = su->chan[i].vol - (su->chan[i].swvol.amt & 31);
                    su->chan[i].vol = (v > 127) ? 127 : (v < -128) ? -128
                                                                   : (int8_t)v;

                    if (su->chan[i].vol & 0x80)
                    {
                        if (su->chan[i].swvol.amt & 64) // wrap enabled
                        {
                            if (su->chan[i].swvol.amt & 128) // bounce enabled
                            {
                                su->chan[i].swvol.amt ^= 32; // flip direction
                                su->chan[i].vol = -su->chan[i].vol;
                            }
                            else
                            {
                                su->chan[i].vol &= ~0x80;
                            }
                        }
                        else
                        {
                            su->chan[i].vol = 0x00; // clamp at 0
                        }
                    }

                    // If not wrapping, clamp at lower bound.
                    if (su->chan[i].vol < su->chan[i].swvol.bound && !(su->chan[i].swvol.amt & 64))
                        su->chan[i].vol = su->chan[i].swvol.bound;
                }
            }
        }

        // 8b) Frequency sweep (flags1 bit4).
        // Implementation is multiplicative (exponential-ish), with bound compared on high byte.
        // swfreq.amt:
        //   bit7: direction (1=up, 0=down)
        //   bits0..6: magnitude
        if ((su->chan[i].flags1 & 16) && su->chan[i].swfreq.speed)
        {
            su->freq_sweep_countdown[i] -= Pm;
            while (su->freq_sweep_countdown[i] <= 0)
            {
                su->freq_sweep_countdown[i] += su->chan[i].swfreq.speed;

                if (su->chan[i].swfreq.amt & 128) // up
                {
                    if (su->chan[i].freq > (0xFFFF - (su->chan[i].swfreq.amt & 127)))
                        su->chan[i].freq = 0xFFFF;
                    else
                    {
                        // Multiply by (1.0 + amt/128).
                        su->chan[i].freq = (su->chan[i].freq * (0x80 + (su->chan[i].swfreq.amt & 127))) >> 7;

                        // Clamp to bound (stored as coarse high-byte limit).
                        if ((su->chan[i].freq >> 8) > su->chan[i].swfreq.bound)
                            su->chan[i].freq = su->chan[i].swfreq.bound << 8;
                    }
                }
                else // down
                {
                    if (su->chan[i].freq < (su->chan[i].swfreq.amt & 127))
                        su->chan[i].freq = 0;
                    else
                    {
                        // Multiply by (1.0 - amt/256).
                        su->chan[i].freq = (su->chan[i].freq * (0xFF - (su->chan[i].swfreq.amt & 127))) >> 8;

                        if ((su->chan[i].freq >> 8) < su->chan[i].swfreq.bound)
                            su->chan[i].freq = su->chan[i].swfreq.bound << 8;
                    }
                }
            }
        }

        // 8c) Cutoff sweep (flags1 bit6).
        // Up: additive; Down: multiplicative decay. Bound compared on high byte.
        // swcut.amt bit7: direction (1=up, 0=down), bits0..6 magnitude.
        if ((su->chan[i].flags1 & 64) && su->chan[i].swcut.speed)
        {
            su->cut_sweep_countdown[i] -= Pm;
            while (su->cut_sweep_countdown[i] <= 0)
            {
                su->cut_sweep_countdown[i] += su->chan[i].swcut.speed;

                if (su->chan[i].swcut.amt & 128) // up
                {
                    if (su->chan[i].cutoff > (0xFFFF - (su->chan[i].swcut.amt & 127)))
                        su->chan[i].cutoff = 0xFFFF;
                    else
                    {
                        su->chan[i].cutoff += (su->chan[i].swcut.amt & 127);

                        if ((su->chan[i].cutoff >> 8) > su->chan[i].swcut.bound)
                            su->chan[i].cutoff = su->chan[i].swcut.bound << 8;
                    }
                }
                else // down
                {
                    if (su->chan[i].cutoff < (su->chan[i].swcut.amt & 127))
                        su->chan[i].cutoff = 0;
                    else
                    {
                        // Multiply by (1.0 - amt/2048).
                        su->chan[i].cutoff = ((2048 - (unsigned int)(su->chan[i].swcut.amt & 127)) * (unsigned int)su->chan[i].cutoff) >> 11;

                        if ((su->chan[i].cutoff >> 8) < su->chan[i].swcut.bound)
                            su->chan[i].cutoff = su->chan[i].swcut.bound << 8;
                    }
                }
            }
        }

        // ------------------------------------------------------------
        // 9) One-shot phase reset request (flags1 bit0)
        // ------------------------------------------------------------
        if (su->chan[i].flags1 & 1)
        {
            su->phase_accum_prev[i] = 0;
            su->phase_reset_countdown[i] = su->chan[i].restimer; // preload timer sync counter
            su->phase_accum[i] = 0;
            su->chan[i].flags1 &= ~1; // clear request
        }

        // Software mute: also clears filter state to avoid stale ringing when unmuted.
        if (su->muted[i])
        {
            su->svf_low[i] = su->svf_high[i] = su->svf_band[i] = 0;
            su->voice_left[i] = su->voice_right[i] = 0;
        }
    }

    // ------------------------------------------------------------
    // Mixdown: sum all channel stereo contributions into output.
    // Uses int64 to avoid overflow during summation; final is clamped to int32 range.
    // ------------------------------------------------------------
    const int64_t L = (su->voice_left[0] + su->voice_left[1] + su->voice_left[2] + su->voice_left[3] + su->voice_left[4] + su->voice_left[5] + su->voice_left[6] + su->voice_left[7]);

    const int64_t R = (su->voice_right[0] + su->voice_right[1] + su->voice_right[2] + su->voice_right[3] + su->voice_right[4] + su->voice_right[5] + su->voice_right[6] + su->voice_right[7]);

    *l = su->mix_left = (int32_t)((int64_t)(minval(INT32_MAX, maxval(INT32_MIN, L))));
    *r = su->mix_right = (int32_t)((int64_t)(minval(INT32_MAX, maxval(INT32_MIN, R))));
}

// -----------------------------------------------------------------------------
// Init: allocate PCM size policy, reset state, build lookup tables.
// -----------------------------------------------------------------------------
void SoundUnit_Init(SoundUnit *su, size_t sampleMemSize)
{
    // PCM size must be power of 2 so pcmpos can wrap with & (pcmSize - 1).
    assert((sampleMemSize & (sampleMemSize - 1)) == 0);

    su->pcm_ram_size = sampleMemSize ? sampleMemSize : 8192;
    assert(su->pcm_ram_size <= sizeof(su->pcm));

    SoundUnit_Reset(su);
    memset(su->pcm, 0, su->pcm_ram_size);

    // Build waveform and pan tables.
    for (size_t i = 0; i < 256; i++)
    {
        // Full-cycle sine across 256 samples: i in [0..255] maps ~[0..2*pi).
        su->wave_sine_lut[i] = sin((i / 128.0f) * M_PI) * 127;

        // Triangle as implemented: 0..127..0 (unipolar).
        su->wave_triangle_lut[i] = (i > 127) ? (255 - i) : (i);

        // Start with "center pan": both gains 127.
        su->pan_gain_lut_l[i] = 127;
        su->pan_gain_lut_r[i] = 127;
    }

    // Pan shaping:
    // - For i=0..127: left gain decreases from 127->0.
    // - For i=128..255: right gain increases from 0->126 (with SCpantabR[128]=0 fixup below).
    for (size_t i = 0; i < 128; i++)
    {
        su->pan_gain_lut_l[i] = 127 - i;
        su->pan_gain_lut_r[128 + i] = i - 1;
    }
    su->pan_gain_lut_r[128] = 0;
}

// -----------------------------------------------------------------------------
// Reset: clears all runtime state (phases, filter states, sweep timers, etc.).
// -----------------------------------------------------------------------------
void SoundUnit_Reset(SoundUnit *su)
{
    for (size_t i = 0; i < 8; i++)
    {
        su->phase_accum[i] = 0;
        su->phase_accum_prev[i] = 0;
        su->phase_reset_countdown[i] = 0;

        su->src_sample_i8[i] = 0;
        su->voice_sample[i] = 0;
        su->voice_left[i] = 0;
        su->voice_right[i] = 0;

        su->svf_low[i] = 0;
        su->svf_high[i] = 0;
        su->svf_band[i] = 0;

        su->dc_tracker_q8[i] = 0;

        // Initialize sweep timers so the first decrement by Pm lands at 0 and triggers only if speed is set.
        su->vol_sweep_countdown[i] = Pm;
        su->freq_sweep_countdown[i] = Pm;
        su->cut_sweep_countdown[i] = Pm;

        su->noise_lfsr[i] = 0xAAAA; // default seed
        su->pcm_phase_accum[i] = 0;
    }

    su->mix_left = 0;
    su->mix_right = 0;

    // Clear all channel registers (freq/vol/pan/flags/cutoff/etc.).
    memset(su->chan, 0, sizeof(struct SUChannel) * 8);
}

// -----------------------------------------------------------------------------
// Register write: byte-addressed write into the packed SUChannel array.
// This assumes the SUChannel memory layout matches your "register map" exactly.
// -----------------------------------------------------------------------------
void SoundUnit_Write(SoundUnit *su, uint8_t addr, uint8_t data)
{
    ((uint8_t *)su->chan)[addr] = data;
}
