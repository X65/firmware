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

/// Originally tSU worked at half of 6.18MHz (NTSC),
/// but this is too much oversampling work for a simple MCU unit.
/// We aim at generating HiFi audio at 48kHz, with just 2x oversampling.
/// This gives us (618000÷2)÷96000 = 3.21875 phase multiplier.
/// To keep it simple, we round it to 3, which keeps the parameter values
/// in the same ballpark as original tSU, with just integer math.

#define SOUND_UNIT_PHASE_MULTIPLIER 3

// channel registers
#define SGU1_CHAN_FREQ_LO         (0x00)
#define SGU1_CHAN_FREQ_HI         (0x01)
#define SGU1_CHAN_VOL             (0x02)
#define SGU1_CHAN_PAN             (0x03)
#define SGU1_CHAN_FLAGS0          (0x04)
#define SGU1_CHAN_FLAGS1          (0x05)
#define SGU1_CHAN_CUTOFF_LO       (0x06)
#define SGU1_CHAN_CUTOFF_HI       (0x07)
#define SGU1_CHAN_DUTY            (0x08)
#define SGU1_CHAN_RESON           (0x09)
#define SGU1_CHAN_PCMPOS_LO       (0x0A)
#define SGU1_CHAN_PCMPOS_HI       (0x0B)
#define SGU1_CHAN_PCMBND_LO       (0x0C)
#define SGU1_CHAN_PCMBND_HI       (0x0D)
#define SGU1_CHAN_PCMRST_LO       (0x0E)
#define SGU1_CHAN_PCMRST_HI       (0x0F)
#define SGU1_CHAN_SWFREQ_SPEED_LO (0x10)
#define SGU1_CHAN_SWFREQ_SPEED_HI (0x11)
#define SGU1_CHAN_SWFREQ_AMT      (0x12)
#define SGU1_CHAN_SWFREQ_BOUND    (0x13)
#define SGU1_CHAN_SWVOL_SPEED_LO  (0x14)
#define SGU1_CHAN_SWVOL_SPEED_HI  (0x15)
#define SGU1_CHAN_SWVOL_AMT       (0x16)
#define SGU1_CHAN_SWVOL_BOUND     (0x17)
#define SGU1_CHAN_SWCUT_SPEED_LO  (0x18)
#define SGU1_CHAN_SWCUT_SPEED_HI  (0x19)
#define SGU1_CHAN_SWCUT_AMT       (0x1A)
#define SGU1_CHAN_SWCUT_BOUND     (0x1B)
#define SGU1_CHAN_SPECIAL1C       (0x1C)
#define SGU1_CHAN_SPECIAL1D       (0x1D)
#define SGU1_CHAN_RESTIMER_LO     (0x1E)
#define SGU1_CHAN_RESTIMER_HI     (0x1F)

// channel control bits
#define SGU1_FLAGS0_WAVE_SHIFT    (0)
#define SGU1_FLAGS0_WAVE_MASK     (0x7 << SGU1_FLAGS0_WAVE_SHIFT)
#define SGU1_FLAGS0_PCM_SHIFT     (3)
#define SGU1_FLAGS0_PCM_MASK      (0x1 << SGU1_FLAGS0_PCM_SHIFT)
#define SGU1_FLAGS0_CONTROL_SHIFT (4)
#define SGU1_FLAGS0_CONTROL_MASK  (0xF << SGU1_FLAGS0_CONTROL_SHIFT)
#define SGU1_FLAGS0_CTL_RING_MOD  (1 << 4)
#define SGU1_FLAGS0_CTL_NSLOW     (1 << 5)
#define SGU1_FLAGS0_CTL_NSHIGH    (1 << 6)
#define SGU1_FLAGS0_CTL_NSBAND    (1 << 7)

#define SGU1_FLAGS0_WAVE_PULSE          (0)
#define SGU1_FLAGS0_WAVE_SAWTOOTH       (1)
#define SGU1_FLAGS0_WAVE_SINE           (2)
#define SGU1_FLAGS0_WAVE_TRIANGLE       (3)
#define SGU1_FLAGS0_WAVE_NOISE          (4)
#define SGU1_FLAGS0_WAVE_PERIODIC_NOISE (5)
#define SGU1_FLAGS0_WAVE_XOR_SINE       (6)
#define SGU1_FLAGS0_WAVE_XOR_TRIANGLE   (7)

#define SGU1_FLAGS1_PHASE_RESET        (1 << 0)
#define SGU1_FLAGS1_FILTER_PHASE_RESET (1 << 1)
#define SGU1_FLAGS1_PCM_LOOP           (1 << 2)
#define SGU1_FLAGS1_TIMER_SYNC         (1 << 3)
#define SGU1_FLAGS1_FREQ_SWEEP         (1 << 4)
#define SGU1_FLAGS1_VOL_SWEEP          (1 << 5)
#define SGU1_FLAGS1_CUT_SWEEP          (1 << 6)

#define SGU1_VOL_SWEEP_INC    (1 << 5)
#define SGU1_VOL_SWEEP_WRAP   (1 << 6)
#define SGU1_VOL_SWEEP_BOUNCE (1 << 7)

typedef struct
{
    // ------------------------------------------------------------
    // PRIVATE (internal state / lookup tables)
    // ------------------------------------------------------------

    // 8-bit waveform lookup tables (one full cycle, 256 steps).
    // Note: wave_sine_lut is bipolar (-127..127), wave_triangle_lut as built is 0..127 (unipolar).
    int8_t wave_sine_lut[256];
    int8_t wave_triangle_lut[256];

    // Panning lookup tables (0..127-ish gain factors).
    // Index is chan[i].pan interpreted as uint8_t.
    // Output is scaled by >>8 later, so these behave like Q0.8-ish gains.
    int8_t pan_gain_lut_l[256];
    int8_t pan_gain_lut_r[256];

    // Phase accumulator per channel (fixed-point).
    // phase_accum[i] advances by freq*Pm per produced sample.
    // phase_accum_prev[i] is previous phase, used to detect "phase region" changes for noise stepping.
    uint32_t phase_accum[8];
    uint32_t phase_accum_prev[8];

    // Phase reset "rest timer" countdown accumulator (in the same tick domain as Pm).
    // When enabled, phase_reset_countdown is decremented each generated sample and forces phase/LFSR reset on expiry.
    int32_t phase_reset_countdown[8];

    // Noise generator state per channel.
    // For SGU1_FLAGS0_WAVE_NOISE uses 32-bit LFSR taps.
    // For SGU1_FLAGS0_WAVE_PERIODIC_NOISE uses a small (6-bit-ish) LFSR constructed in the low bits.
    uint32_t noise_lfsr[8];

    // src_sample_i8[i] = raw oscillator/PCM sample for channel i (nominally -128..127).
    // voice_sample[i] = processed sample after volume/filter/DC-block (higher precision int32).
    int8_t src_sample_i8[8];
    int32_t voice_sample[8];

    // Per-channel stereo contributions after pan (still int32).
    int32_t voice_left[8];
    int32_t voice_right[8];

    // State-variable filter state per channel.
    // svf_low: low-pass integrator state
    // svf_high: high-pass output state
    // svf_band: band-pass integrator state
    int32_t svf_low[8];
    int32_t svf_high[8];
    int32_t svf_band[8];

    // DC blocker state per channel (stored in Q8 fixed-point in dc_block_q8()).
    int32_t dc_tracker_q8[8];

    // Last mixed output sample (left/right), also returned by NextSample().
    int32_t mix_left, mix_right;

    // Size of PCM RAM actually used (must be power of 2, <= pcm[] size).
    uint32_t pcm_ram_size;

    // ------------------------------------------------------------
    // PUBLIC (register-visible + helpers)
    // ------------------------------------------------------------

    // Sweep countdown timers (per channel). They count down by Pm each generated sample.
    // When <= 0, they "tick" and apply one sweep step, then reload by sw*.speed.
    int32_t vol_sweep_countdown[8];  // volume sweep timer accumulator
    int32_t freq_sweep_countdown[8]; // frequency sweep timer accumulator
    int32_t cut_sweep_countdown[8];  // cutoff sweep timer accumulator

    // PCM fractional position accumulator per channel.
    // pcm_phase_accum integrates playback rate; when it crosses 32768, pcmpos advances by 1.
    // This is essentially a fixed-point resampling stepper.
    int32_t pcm_phase_accum[8];

    // Channel "register file" (what SoundUnit_Write() writes into, byte-addressed).
    struct SUChannel
    {
        // freq: 16-bit phase increment / playback rate parameter (interpretation depends on mode).
        uint16_t freq;

        // vol: signed 8-bit volume scalar. ns*vol is used; sign allows inversion.
        int8_t vol;

        // pan: 0..255 index into SCpantabL/R.
        int8_t pan;

        // flags0:
        //  - bits 0..2: waveform select (0..7)
        //  - bit 3: PCM enable (when set, ns = pcm[pcmpos])
        //  - bit 4: ring mod enable (multiply by next channel's raw sample)
        //  - bits 5..7: filter mode selects (LP/HP/BP) (implemented as bitmask picks)
        uint8_t flags0;

        // flags1:
        //  - bit 0: one-shot phase reset request (handled at end of channel processing)
        //  - bit 2: PCM loop enable
        //  - bit 3: timer sync enable (enables restimer-based periodic phase reset)
        //  - bit 4: freq sweep enable
        //  - bit 5: vol sweep enable
        //  - bit 6: cutoff sweep enable
        uint8_t flags1;

        // cutoff: filter cutoff control (scaled to ff inside filter section).
        uint16_t cutoff;

        // duty: pulse width (0..127). Upper nibble is also reused to pick periodic-noise mode / rate.
        uint8_t duty;

        // reson: resonance amount (0..255). Used as (256 - reson) feedback term.
        uint8_t reson;

        // PCM playback pointers.
        uint16_t pcmpos; // current sample position
        uint16_t pcmbnd; // boundary/end position
        uint16_t pcmrst; // loop restart position

        // Sweep parameter blocks:
        // speed: period in "ticks" (same domain as Pm) between sweep steps
        // amt:   step amount + direction/mode bits (interpretation differs by sweep type)
        // bound: limit value (coarse, often compared against high byte of freq/cutoff)
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

        // Extra spare "registers" (used by some engines as generic per-channel params).
        uint8_t special1C;
        uint8_t special1D;

        // restimer: period for periodic phase reset when SGU1_FLAGS1_TIMER_SYNC is set.
        uint16_t restimer;
    } chan[8];

    // PCM sample memory (signed 8-bit).
    int8_t pcm[65536];

    // Per-channel mute (software-side, not part of chip spec).
    bool muted[8];

} SoundUnit;

void SoundUnit_Init(SoundUnit *su, size_t sampleMemSize);
void SoundUnit_Reset(SoundUnit *su);

void SoundUnit_Write(SoundUnit *su, uint8_t addr, uint8_t data);

void SoundUnit_NextSample(SoundUnit *su, int32_t *l, int32_t *r);

// Convenience getter: returns mono downmix of current per-channel post-pan samples (averaged).
// This is not used in NextSample, but useful for taps/meters/debug.
static inline int32_t __attribute__((always_inline))
SoundUnit_GetSample(SoundUnit *su, uint8_t ch)
{
    int32_t ret = (su->voice_left[ch] + su->voice_right[ch]) >> 1;
    if (ret < -32768)
        ret = -32768;
    if (ret > 32767)
        ret = 32767;
    return ret;
}
