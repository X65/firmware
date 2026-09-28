/*
 * Copyright (c) 2026 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _PCM_H_
#define _PCM_H_

/* PCM sample player on the SGTL5000 codec.
 *
 * gen1 has no SGU-1: the $FEC0-$FEFF window belongs to the SD-1 (YMF825).
 * The SD-1 registers #35-#62 at $FEE3-$FEFE are read-only EQ coefficient
 * readback ports, so the PCM player takes their place. $FEC0-$FEE2 and
 * $FEFF still reach the SD-1.
 *
 * Four voices play from one sample bank in RIA SRAM (24 KiB), mixed at the
 * codec's frame rate (PCM_OUTPUT_RATE) and sent to the I2S DAC by DMA.
 * The interface follows the SGU-1 PCM path and service bank: an
 * auto-incrementing upload port, a master volume, and per-voice
 * position / end / loop words with GATE and LOOP flags.
 *
 * Registers (bank 0; words are little-endian):
 *
 *   $FEE3  PCM_ID      R    $50 ('P'). Probe: an SD-1 without the player
 *                           reads CEQ00[23:16] here ($10 after reset).
 *   $FEE4  PCM_BUSY    R    bit n set while voice n is gated and not
 *                           parked at END.
 *   $FEE5  MASTER_VOL  R/W  $00 mute .. $FF unity. Reset $FF.
 *   $FEE6  SAMPLE_OFF  R/W  upload byte offset, low byte
 *   $FEE7              R/W  upload byte offset, high byte (bit 7 ignored)
 *   $FEE8  SAMPLE_DATA R/W  byte at the upload offset; every access
 *                           advances the offset, wrapping $7FFF -> $0000.
 *                           Past the bank reads $00 and drops writes.
 *   $FEE9  VOICE_SEL   R/W  voice 0-3 shown at $FEEA-$FEF5
 *   $FEEA  V_FLAGS     R/W  [0] GATE: play; clear pauses, position frozen
 *                           [1] LOOP: at END jump to V_LOOP
 *                           [2] UNSIGNED: 8-bit unsigned samples
 *                           [3] NIBBLE: 4-bit unsigned samples, two a byte,
 *                               low nibble first, level = nibble * 17
 *                           neither: 8-bit signed samples (SGU-1 format)
 *   $FEEB  V_VOL       R/W  $00 mute .. $FF unity
 *   $FEEC  V_PAN       R/W  int8: -128 left, 0 centre (both sides at
 *                           full level), +127 right
 *   $FEED  BANK_PAGES  R    sample bank size in 256-byte pages ($60)
 *   $FEEE  V_RATE      R/W  playback rate in Hz; PCM_OUTPUT_RATE is 1:1.
 *                           Faster rates skip samples (no filtering).
 *   $FEF0  V_POS       R/W  current sample position, advanced live
 *   $FEF2  V_END       R/W  end boundary (SGU-1 PCMBND)
 *   $FEF4  V_LOOP      R/W  restart point (SGU-1 PCMRST)
 *   $FEF6-$FEFE        R    reserved, $00
 *
 * Positions count samples, not bytes: an 8-bit voice addresses byte
 * pos & $7FFF, a 4-bit voice byte pos >> 1. Samples past the bank are
 * silence.
 *
 * Each output frame a gated voice that has not parked outputs the sample at
 * V_POS, then steps V_POS forward by V_RATE / PCM_OUTPUT_RATE. Stepping
 * onto V_END jumps to V_LOOP when LOOP is set; otherwise the voice parks
 * at V_END, silent, and its PCM_BUSY bit clears.
 *
 * Starting a sound: with GATE clear write V_POS, V_END, V_LOOP, V_RATE,
 * V_VOL and V_PAN, then write V_FLAGS with GATE set. Writing V_POS
 * restarts the rate accumulator.
 *
 * CPU stop and reboot clear every GATE, select voice 0, set MASTER_VOL to
 * $FF and the upload offset to 0. Each voice returns to VOL $FF, PAN 0,
 * RATE PCM_OUTPUT_RATE and POS, END, LOOP 0. Sample memory is preserved.
 */

#include <stdint.h>

// The codec's frame rate, set in aud_i2s_reg_init() (sys/aud.c)
#define PCM_OUTPUT_RATE 8000

#define PCM_REG_FIRST 0x23 // SD-1 register #35, $FEE3
#define PCM_REG_LAST  0x3E // SD-1 register #62, $FEFE

/* Kernel events
 */

void pcm_init(void);
void pcm_stop(void);

// Register access for the bus ISR. reg is the $FEC0 window offset,
// PCM_REG_FIRST..PCM_REG_LAST.
uint8_t pcm_reg_read(uint8_t reg);
void pcm_reg_write(uint8_t reg, uint8_t data);

#endif /* _PCM_H_ */
