/* sgu.c/sgu.h - SGU-1 Sound Generator Unit 1
 *
 * Copyright (C) 2025 Tomasz "smokku" Sterna
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

#ifndef SGU_EG_DEBUG
#define SGU_EG_DEBUG 0
#endif

/**
 * SGU-1 - Sound Generator Unit 1
 *
 * SGU-1 is a hybrid sound synthesis unit combining features
 * from additive operator synthesis and subtractive filtering.
 *
 * - Per-voice sound is produced by a 4-operator ESFM-style chain;
 *   each operator can act as modulator and/or carrier via per-operator MOD
 *   (phase modulation from previous operator, with feedback on operator 0)
 *   and per-operator OUT (direct contribution to the voice mix).
 * - One base pitch per channel. Operator pitch is derived from channel pitch
 *   using OPL-style MUL plus per-operator DT detune.
 * - Optional per-operator fixed-frequency mode (OPZ-like) enabled by a flag;
 *   fixed frequency is selected by reinterpreting existing MUL+DT fields.
 * - Per-operator level and timbre controls: TL attenuation with
 *   key-level scaling (KSL), KSR/rate scaling, waveform selection,
 *   tremolo/vibrato enable and depth, plus optional hard sync
 *   and ring modulation to the previous operator.
 * - Per-operator envelope uses AR/DR/SR/SL/RR for improved percussive control.
 * - Register layout is compact and Yamaha-like:
 *   8 bytes per operator, closely aligned with OPL/OPL3-style ergonomics
 *   and ESFM routing flexibility, while enabling reasonable reproduction
 *   of OPN/M-family chiptunes and broadly similar FM/subtractive workflows.
 * - Operator output is mixed into a per-voice pipeline that applies
 *   SID-like shaping (multimode filter with cutoff/resonance),
 *   plus per-channel volume/frequency/filter slides with optional wrap/bounce behaviors.
 * - Channel supports waveform or PCM sample playback; one-shot and looping,
 *   per-channel volume control and stereo panning.
 */

#define SGU_CHIP_CLOCK     (48000) // 48kHz
#define SGU_AUDIO_CHANNELS (2)

// Alpha = 0.99869 scaled to 16-bit (0.99869 * 65536)
#define SGU_ALPHA_RC_DECAY_Q16 (65450)

#define SGU_CHNS        (9)
#define SGU_CH_REGS     (32)
#define SGU_OP_PER_CH   (4)
#define SGU_OP_REGS     (8)
#define SGU_REGS_PER_CH (SGU_OP_PER_CH * SGU_OP_REGS + SGU_CH_REGS)

#define SGU_PCM_RAM_SIZE    (0x10000) // 64KB PCM RAM
#define SGU_WAVEFORM_LENGTH (0x400)   // 1024 samples per waveform

/*
### Operator bit layout

```
R0  [7]TRM  [6]VIB  [5]FIX  [4]---  [3:0]MUL
R1  [7:6]KSL        [5:0]TL(0..63)

R2  [7:4]AR_lo4                     [3:0]DR_lo4
R3  [7:4]SL(0..15)                  [3:0]RR(0..15)

R4  [7:5]DT                 [4:0]SR(0..31)

R5  [7:5]DELAY              [4:3]KSR            [2:0]WPAR

R6  [7]TRMD [6]VIBD [5]SYNC [4]RING [3:1]MOD [0]TL_msb
R7  [7:5]OUT                [4]AR_msb [3]DR_msb [2:0]WAVE
```

### Assembled parameter widths (OPL/ESFM-like; OPN/OPM-like where it matters)

* **AR** = `(AR_msb<<4) | AR_lo4`  → 0..31 (5-bit)
* **DR (D1R)** = `(DR_msb<<4) | DR_lo4` → 0..31 (5-bit)
* **SR (D2R)** = `SR` → 0..31 (5-bit)
* **SL (D1L)** = `SL` → 0..15 (4-bit)
* **RR** = `RR` → 0..15 (4-bit)
* **TL** = `(TL_msb<<6) | TL_lo6` → 0..127 (7-bit, closer to OPN/OPM feel)

This keeps OPL-style packing in R2/R3 intact (low nibbles),
but upgrades AR/DR to 5-bit and adds true SR.

---

## Operator frequency behavior (single channel pitch)

Let channel base frequency be `f_ch` (from channel FREQ).

### Ratio mode (FIX=0)

* `f_op = f_ch * mul(MUL) + detune(DT)`
* `MUL` uses OPL semantics (0 => 0.5×, 1..15 => 1×..15×).
* `DT` is per-operator detune (3 bits) based on keycode.

### Fixed mode (FIX=1, OPZ-like)

* Operator ignores `f_ch`.
* Fixed frequency uses MUL as a base and DT as a scale:
  `freq16 = (8 + (MUL * 247 + 7) / 15) << DT`  (range ≈ 8..32640).
* In fixed mode, DT does not act as detune.

---

### Wave parameter WPAR

Additional WAVE form related parameter (per-operator, 3 bits)

- PULSE
  Selects channel pulse width or a fixed per-operator pulse width:
  0 => use channel pulse width
  1..7 => x/8 pulse width (x low units, 8-x high units)
- SAW
  bit 0 => selects rising/falling (inverted value)
  bits 1..2 => quantize wave table look-up; zero { 0, 4, 6, 8 } last bits
- SINE, TRIANGLE
  bit 0 => skew waveform peak position using channel duty value
- PERIODIC_NOISE
  bits 0..1 => LFSR tap configuration (0-3) for different timbres
  This is per-operator, allowing different noise timbres in each operator


## MOD / OUT routing

### MOD (R6[3:1])

* Sets how much this operator is phase-modulated by the **previous operator** (6 dB steps).
* For operator 0, MOD becomes **feedback level** (ESFM behavior).

### OUT (R7[7:5])

* Sets this operator’s direct contribution to the channel mix (6 dB steps).
* By choosing MOD/OUT per operator you can realize OPL-style 2-op algorithms, OPN/OPM-ish 4-op structures, and hybrids (operator can be both modulator and carrier).

*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Operator bitfields
// -----------------------------------------------------------------------------

// R0: [7]TRM [6]VIB [5]FIX [4]--- [3:0]MUL
#define SGU_OP0_MUL_MASK 0x0F
#define SGU_OP0_FIX_BIT  0x20
#define SGU_OP0_VIB_BIT  0x40
#define SGU_OP0_TRM_BIT  0x80

#define SGU_OP0_TRM(reg) ((reg) & SGU_OP0_TRM_BIT)
#define SGU_OP0_VIB(reg) ((reg) & SGU_OP0_VIB_BIT)
#define SGU_OP0_FIX(reg) ((reg) & SGU_OP0_FIX_BIT)
#define SGU_OP0_MUL(reg) ((reg) & SGU_OP0_MUL_MASK)

// R1: [7:6]KSL [5:0]TL_lo6
#define SGU_OP1_TL_MASK   0x3F
#define SGU_OP1_TL_OFFSET 6
#define SGU_OP1_KSL_MASK  0xC0
#define SGU_OP1_KSL_SHIFT 6

#define SGU_OP1_KSL(reg)    (((reg) & SGU_OP1_KSL_MASK) >> SGU_OP1_KSL_SHIFT)
#define SGU_OP1_TL_LO6(reg) ((reg) & SGU_OP1_TL_MASK)

// R2: [7:4]AR_lo4 [3:0]DR_lo4
#define SGU_OP2_DR_MASK   0x0F
#define SGU_OP2_DR_OFFSET 4
#define SGU_OP2_AR_MASK   0xF0
#define SGU_OP2_AR_SHIFT  4
#define SGU_OP2_AR_OFFSET 4

#define SGU_OP2_AR_LO4(reg) (((reg) & SGU_OP2_AR_MASK) >> SGU_OP2_AR_SHIFT)
#define SGU_OP2_DR_LO4(reg) ((reg) & SGU_OP2_DR_MASK)

// R3: [7:4]SL [3:0]RR
#define SGU_OP3_RR_MASK  0x0F
#define SGU_OP3_SL_MASK  0xF0
#define SGU_OP3_SL_SHIFT 4

#define SGU_OP3_SL(reg) (((reg) & SGU_OP3_SL_MASK) >> SGU_OP3_SL_SHIFT)
#define SGU_OP3_RR(reg) ((reg) & SGU_OP3_RR_MASK)

// R4: [7:5]DT [4:0]SR
#define SGU_OP4_SR_MASK  0x1F
#define SGU_OP4_DT_MASK  0xE0
#define SGU_OP4_DT_SHIFT 5

#define SGU_OP4_SR(reg) ((reg) & SGU_OP4_SR_MASK)
#define SGU_OP4_DT(reg) (((reg) & SGU_OP4_DT_MASK) >> SGU_OP4_DT_SHIFT)

// R5: [7:5]DELAY [4:3]KSR [2:0]WPAR
#define SGU_OP5_WPAR_MASK   0x07
#define SGU_OP5_KSR_MASK    0x18
#define SGU_OP5_KSR_SHIFT   3
#define SGU_OP5_DELAY_MASK  0xE0
#define SGU_OP5_DELAY_SHIFT 5

#define SGU_OP5_WPAR(reg)  ((reg) & SGU_OP5_WPAR_MASK)
#define SGU_OP5_DELAY(reg) (((reg) & SGU_OP5_DELAY_MASK) >> SGU_OP5_DELAY_SHIFT)
#define SGU_OP5_KSR(reg)   (((reg) & SGU_OP5_KSR_MASK) >> SGU_OP5_KSR_SHIFT)

// R6: [7]TRMD [6]VIBD [5]SYNC [4]RING [3:1]MOD [0]TL_msb
#define SGU_OP6_TL_MSB_BIT 0x01
#define SGU_OP6_MOD_MASK   0x0E
#define SGU_OP6_MOD_SHIFT  1
#define SGU_OP6_RING_BIT   0x10
#define SGU_OP6_SYNC_BIT   0x20
#define SGU_OP6_VIBD_BIT   0x40
#define SGU_OP6_TRMD_BIT   0x80

#define SGU_OP6_TRMD(reg)   ((reg) & SGU_OP6_TRMD_BIT)
#define SGU_OP6_VIBD(reg)   ((reg) & SGU_OP6_VIBD_BIT)
#define SGU_OP6_SYNC(reg)   ((reg) & SGU_OP6_SYNC_BIT)
#define SGU_OP6_RING(reg)   ((reg) & SGU_OP6_RING_BIT)
#define SGU_OP6_MOD(reg)    (((reg) & SGU_OP6_MOD_MASK) >> SGU_OP6_MOD_SHIFT)
#define SGU_OP6_TL_MSB(reg) ((reg) & SGU_OP6_TL_MSB_BIT)
#define SGU_OP16_TL(reg1, reg6) \
    (SGU_OP1_TL_LO6(reg1) | (SGU_OP6_TL_MSB(reg6) << SGU_OP1_TL_OFFSET))

// R7: [7:5]OUT [4]AR_msb [3]DR_msb [2:0]WAVE
#define SGU_OP7_WAVE_MASK    0x07
#define SGU_OP7_DR_MSB_BIT   0x08
#define SGU_OP7_DR_MSB_SHIFT 3
#define SGU_OP7_AR_MSB_BIT   0x10
#define SGU_OP7_AR_MSB_SHIFT 4
#define SGU_OP7_OUT_MASK     0xE0
#define SGU_OP7_OUT_SHIFT    5

#define SGU_OP7_OUT(reg)    (((reg) & SGU_OP7_OUT_MASK) >> SGU_OP7_OUT_SHIFT)
#define SGU_OP7_AR_MSB(reg) (((reg) & SGU_OP7_AR_MSB_BIT) >> SGU_OP7_AR_MSB_SHIFT)
#define SGU_OP7_DR_MSB(reg) (((reg) & SGU_OP7_DR_MSB_BIT) >> SGU_OP7_DR_MSB_SHIFT)
#define SGU_OP7_WAVE(reg)   ((reg) & SGU_OP7_WAVE_MASK)
#define SGU_OP27_AR(reg2, reg7) \
    (SGU_OP2_AR_LO4(reg2) | (SGU_OP7_AR_MSB(reg7) << SGU_OP2_AR_OFFSET))
#define SGU_OP27_DR(reg2, reg7) \
    (SGU_OP2_DR_LO4(reg2) | (SGU_OP7_DR_MSB(reg7) << SGU_OP2_DR_OFFSET))

// -----------------------------------------------------------------------------
// Channel register set (per channel)
// -----------------------------------------------------------------------------
#define SGU1_CHN_FREQ_L       (0x00)
#define SGU1_CHN_FREQ_H       (0x01)
#define SGU1_CHN_VOL          (0x02)
#define SGU1_CHN_PAN          (0x03)
#define SGU1_CHN_FLAGS0       (0x04)
#define SGU1_CHN_FLAGS1       (0x05)
#define SGU1_CHN_CUTOFF_L     (0x06)
#define SGU1_CHN_CUTOFF_H     (0x07)
#define SGU1_CHN_DUTY         (0x08)
#define SGU1_CHN_RESON        (0x09)
#define SGU1_CHN_PCM_POS_L    (0x0A)
#define SGU1_CHN_PCM_POS_H    (0x0B)
#define SGU1_CHN_PCM_END_L    (0x0C)
#define SGU1_CHN_PCM_END_H    (0x0D)
#define SGU1_CHN_PCM_RST_L    (0x0E)
#define SGU1_CHN_PCM_RST_H    (0x0F)
#define SGU1_CHN_SWFREQ_SPD_L (0x10)
#define SGU1_CHN_SWFREQ_SPD_H (0x11)
#define SGU1_CHN_SWFREQ_AMT   (0x12)
#define SGU1_CHN_SWFREQ_BND   (0x13)
#define SGU1_CHN_SWVOL_SPD_L  (0x14)
#define SGU1_CHN_SWVOL_SPD_H  (0x15)
#define SGU1_CHN_SWVOL_AMT    (0x16)
#define SGU1_CHN_SWVOL_BND    (0x17)
#define SGU1_CHN_SWCUT_SPD_L  (0x18)
#define SGU1_CHN_SWCUT_SPD_H  (0x19)
#define SGU1_CHN_SWCUT_AMT    (0x1A)
#define SGU1_CHN_SWCUT_BND    (0x1B)
#define SGU1_CHN_RESTIMER_L   (0x1C)
#define SGU1_CHN_RESTIMER_H   (0x1D)
#define SGU1_CHN_SPECIAL1     (0x1E)
#define SGU1_CHN_SPECIAL2     (0x1F)

// channel control bits
#define SGU1_FLAGS0_CTL_KEYON     (1 << 0)
#define SGU1_FLAGS0_PCM_SHIFT     (3)
#define SGU1_FLAGS0_PCM_MASK      (0x1 << SGU1_FLAGS0_PCM_SHIFT)
#define SGU1_FLAGS0_CONTROL_SHIFT (4)
#define SGU1_FLAGS0_CONTROL_MASK  (0xF << SGU1_FLAGS0_CONTROL_SHIFT)
#define SGU1_FLAGS0_CTL_RING_MOD  (1 << 4)
#define SGU1_FLAGS0_CTL_NSLOW     (1 << 5)
#define SGU1_FLAGS0_CTL_NSHIGH    (1 << 6)
#define SGU1_FLAGS0_CTL_NSBAND    (1 << 7)

#define SGU1_FLAGS1_PHASE_RESET        (1 << 0)
#define SGU1_FLAGS1_FILTER_PHASE_RESET (1 << 1)
#define SGU1_FLAGS1_PCM_LOOP           (1 << 2)
#define SGU1_FLAGS1_TIMER_SYNC         (1 << 3)
#define SGU1_FLAGS1_FREQ_SWEEP         (1 << 4)
#define SGU1_FLAGS1_VOL_SWEEP          (1 << 5)
#define SGU1_FLAGS1_CUT_SWEEP          (1 << 6)

// -----------------------------------------------------------------------------
// Notes on behavior (implementation-level, not register-level)
// - Operator envelope is AR -> DR toward SL, then SR while key held, then RR on key-off.
// - MOD is phase modulation gain from previous op; op0 uses MOD as feedback gain.
// - SYNC resets this op phase on previous op wrap; RING multiplies by previous op sign/value.
// - FIX mode ignores channel pitch and derives a fixed frequency from MUL+DT.
// -----------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

// Waveform types for operator R7[2:0] (0..5 implemented)
// - WAVE_NOISE: 32-bit LFSR white noise, SID-compatible clocking (freq16 * 0.9537 Hz)
// - WAVE_PERIODIC_NOISE: 6-bit LFSR metallic/tonal noise
//     Frequency: channel freq16 × operator multiplier (R0[3:0])
//     Timbre: channel duty[5:4] selects LFSR tap configuration:
//       0: taps 3,4     (~31 states)
//       1: taps 2,3     (~31 states)
//       2: taps 0,2,3   (different timbre)
//       3: taps 0,2,3,5 (max length ~63 states)
typedef enum : uint8_t
{
    SGU_WAVE_SINE = 0,
    SGU_WAVE_TRIANGLE = 1,
    SGU_WAVE_SAWTOOTH = 2,
    SGU_WAVE_PULSE = 3,
    SGU_WAVE_NOISE = 4,
    SGU_WAVE_PERIODIC_NOISE = 5,
    SGU_WAVE_XOR_SINE = 6,     // reserved (unimplemented)
    SGU_WAVE_XOR_TRIANGLE = 7, // reserved (unimplemented)
} sgu_waveform_t;

// Envelope states
enum envelope_state : uint8_t
{
    SGU_EG_ATTACK = 0,
    SGU_EG_DECAY = 1,
    SGU_EG_SUSTAIN = 2,
    SGU_EG_RELEASE = 3,
    SGU_EG_STATES = 4
};

struct SGU
{
    struct SGU_CH
    {
        struct SGU_OP
        {
            // R0: [7]TRM [6]VIB [5]FIX [4]--- [3:0]MUL
            // - TRM/VIB enable LFO AM/PM (depth set in R6).
            // - FIX selects fixed-frequency mode.
            // - MUL is OPL-style multiplier (0 => 0.5×, 1..15 => 1×..15×).
            uint8_t reg0;

            // R1: [7:6]KSL [5:0]TL
            // - TL is output attenuation in 0.75 dB steps.
            // - KSL scales attenuation by pitch (OPL-style).
            uint8_t reg1;

            // R2: [7:4]AR_lo4 [3:0]DR_lo4
            // R3: [7:4]SL [3:0]RR
            // R4: [7:5]DT [4:0]SR
            // ADSR envelope:
            // Attack, Decay, Sustain Level and Rate, Release Rate
            // - AR/DR/SR are 5-bit values using R7 msbs; SL/RR are 4-bit.
            // - DT selects detune table entry (signed adjustment).
            uint8_t reg2;
            uint8_t reg3;
            uint8_t reg4;

            // R5: [7:5]DELAY [4:3]KSR [2:0]WPAR
            // - DELAY adds 2**(DELAY + 8) samples to key-on (phase + envelope).
            // - KSR selects rate-scaling strength (2-bit).
            // - WPAR is a per-waveform shape parameter.
            uint8_t reg5;

            // R6: [7]TRMD [6]VIBD [5]SYNC [4]RING [3:1]MOD [0]TL_msb
            // - TRMD/VIBD set LFO depth.
            // - SYNC hard-syncs to previous operator wrap.
            // - RING multiplies by previous operator output.
            // - MOD sets phase modulation depth (6 dB steps); op0 uses feedback.
            // - TL_msb extends total level to 7 bits.
            uint8_t reg6;

            // R7: [7:5]OUT [4]AR_msb [3]DR_msb [2:0]WAVE
            // - AR/DR msb extend envelope rates to 5 bits.
            // - OUT sets direct mix level (6 dB steps).
            // - WAVE selects waveform (0..5 implemented).
            uint8_t reg7;

        } op[SGU_OP_PER_CH];

        uint16_t freq; // 16-bit phase increment / playback rate
        int8_t vol;    // signed 8-bit; sign allows inversion.
        int8_t pan;    // positive Right, negative Left

        // flags0:
        //  - bit 0: (KEY) triggers key-on for the associated channel,
        //    starting the envelope generator and resetting the signal phase.
        //  - bit 3: PCM enable (when set, src = pcm[pcmpos])
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

        // duty[6:0]: pulse width for WAVE_PULSE (0..127), where low = duty steps, high = 128 - duty
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

        // restimer: period for periodic phase reset when SGU_FLAGS1_TIMER_SYNC is set.
        uint16_t restimer;

        // ### Used for implementation specific purposes.
        // Default function in X65 deployment special2 changes the channel mapped into
        // CPU memory space, which consists of 64 registers only
        // and would not fit all channels at once.
        // Channel FFh is special, as it maps service registers into memory space.
        // Chip identifier, UniqueID and mixer/DSP controls.
        uint8_t special1;
        uint8_t special2;

    } chan[SGU_CHNS];

    // PCM sample memory (signed 8-bit).
    int8_t pcm[SGU_PCM_RAM_SIZE];

    // Per-channel mute (software-side, not part of chip spec).
    bool muted[SGU_CHNS];

    // ------ private generator state ------
    uint32_t sample_counter; // sample clock ticks

    // internal state - global LFO
    uint16_t m_lfo_am_counter; // LFO AM counter
    uint16_t m_lfo_pm_counter; // LFO PM counter
    uint8_t m_lfo_am;          // current LFO AM value

    // channels internal state
    struct sgu_ch_state
    {
        int16_t op0_fb;                                    // feedback memory for first operator
        uint32_t phase[SGU_OP_PER_CH];                     // current phase value (10.22 format)
        uint32_t prev_phase[SGU_OP_PER_CH];                // previous phase value (for noise boundary detection)
        int16_t value[SGU_OP_PER_CH];                      // current output value
        uint16_t envelope_attenuation[SGU_OP_PER_CH];      // computed envelope attenuation (4.6 format)
        enum envelope_state envelope_state[SGU_OP_PER_CH]; // current envelope state
        uint32_t noise_lfsr[SGU_OP_PER_CH];                // per-operator noise LFSR state
        bool phase_wrap[SGU_OP_PER_CH];                    // phase wrap flag for current sample (for SYNC)
        bool key_state[SGU_OP_PER_CH];                     // current key state: on or off
        bool keyon_live[SGU_OP_PER_CH];                    // live key on state
        bool keyon_gate[SGU_OP_PER_CH];                    // last raw key state (edge detect for delay)
        bool eg_delay_run[SGU_OP_PER_CH];                  // envelope delay active
        uint16_t eg_delay_counter[SGU_OP_PER_CH];          // delay counter (samples)
#if SGU_EG_DEBUG
        uint32_t eg_last_transition[SGU_OP_PER_CH];       // sample index of last state transition
        enum envelope_state eg_last_state[SGU_OP_PER_CH]; // last logged state
#endif
    } m_channel[SGU_CHNS];

    // precomputed waveforms (1024 samples each)
    int16_t waveform_lut[SGU_WAVEFORM_LENGTH / 2];

    // src[i] = raw oscillator sample for channel i (16-bit, used for ring mod).
    // post[i] = processed sample after volume/filter (higher precision int32).
    int16_t src[SGU_CHNS];
    int32_t post[SGU_CHNS];

    // Per-channel stereo contributions after pan (still int32).
    int32_t outL[SGU_CHNS];
    int32_t outR[SGU_CHNS];

    // Last mixed output sample (left/right), also returned by NextSample().
    int32_t L, R;
    int64_t L_in, R_in, L_q16, R_q16; // used for high-pass filtering

    // Size of PCM RAM actually used (must be power of 2, <= pcm[] size).
    uint32_t pcm_size;

    // ------ SID-like channel processing state (post-FM) ------

    // State-variable filter state per channel
    int32_t svf_low[SGU_CHNS];
    int32_t svf_high[SGU_CHNS];
    int32_t svf_band[SGU_CHNS];

    // Sweep countdown timers (decrement each sample, trigger when <= 0)
    int32_t vol_sweep_countdown[SGU_CHNS];
    int32_t freq_sweep_countdown[SGU_CHNS];
    int32_t cut_sweep_countdown[SGU_CHNS];

    // Phase reset countdown for timer sync
    int32_t phase_reset_countdown[SGU_CHNS];

    // PCM phase accumulator for fractional PCM playback
    int32_t pcm_phase_accum[SGU_CHNS];

    // Panning gain lookup tables (computed once in Init)
    uint8_t pan_gain_lut_l[256];
    uint8_t pan_gain_lut_r[256];
};

// -----------------------------------------------------------------------------

void SGU_Init(struct SGU *sgu, size_t sampleMemSize);
void SGU_Reset(struct SGU *sgu);

void SGU_Write(struct SGU *sgu, uint16_t addr13, uint8_t data);

void SGU_NextSample(struct SGU *sgu, int32_t *l, int32_t *r);

// Convenience getter: returns mono downmix of current per-channel post-pan samples (averaged).
// This is not used in NextSample, but useful for taps/meters/debug.
int32_t SGU_GetSample(struct SGU *sgu, uint8_t ch);

#ifdef __cplusplus
}
#endif
