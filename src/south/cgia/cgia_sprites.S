#include "hardware/regs/addressmap.h"
#include "cgia.h" // sprite flags and the palette layout

.syntax unified
.cpu cortex-m33
.thumb

// Put every function in its own ELF section, to permit linker GC
.macro decl_func name
.section .text.\name, "ax"
.global \name
.type \name,%function
.thumb_func
\name:
.endm

//////////////////// SPRITES ////////////////////

#define SPRITE_POS_X_OFFS   0
#define SPRITE_POS_Y_OFFS   2
#define SPRITE_LINES_Y_OFFS 4
#define SPRITE_FLAGS_OFFS   6

// ----- 1bpp sprites: bit test, one byte per column -----

// r0 is output RGB buffer
// r3 is bitmap (8 LSB)
// r4 is foreground color RGB
// changes r0
.macro write_sprite_bmp double
  // need bit shift/test first
  bcc 1f
  // bit is 1 - draw foreground pixel(s)
  stmia r0!, {r4}
.if \double
  stmia r0!, {r4}
.endif
  b 2f
1:  // bit is 0 - draw transparent pixel(s)
.if \double
  adds r0, #8     // move 2 screen pixels
.else
  adds r0, #4     // move 1 screen pixel
.endif
2:
.endm

// changes r0,r3
.macro write_sprite_bmp_ltr double
  lsls r3, #1        // shift-out left 1 bit
  write_sprite_bmp \double
.endm

// changes r0,r3
.macro write_sprite_bmp_rtl double
  lsrs r3, #1        // shift-out right 1 bit
  write_sprite_bmp \double
.endm

// ----- 2/3/4bpp sprites: palette lookup, bpp bytes per column -----

// r0 is output RGB buffer
// r3 is sprite palette (16 entries)
// r5 is column chunk (bpp bytes, MSB pixel first)
// r9 is palette begin address
// changes r0,r6
.macro write_sprite_indexed sh_amt bpp double
  ubfx r6, r5, #\sh_amt, #\bpp
  cbz r6, 1f        // entry 0 is transparent
  ldrb r6, [r3, r6]         // sprite palette entry
  ldr r6, [r9, r6, LSL #2]  // RGB
  stmia r0!, {r6}
.if \double
  stmia r0!, {r6}
.endif
  b 2f
1:
.if \double
  adds r0, #8     // move 2 screen pixels
.else
  adds r0, #4     // move 1 screen pixel
.endif
2:
.endm

// load a column of bpp bytes at r2 into r5, first byte in the high bits
// changes r5,r6
.macro load_sprite_chunk bpp
  ldrb r5, [r2]
.set chunk_byte, 1
.rept \bpp-1
  ldrb r6, [r2, #chunk_byte]
  lsls r5, #8
  orrs r5, r6
.set chunk_byte, chunk_byte+1
.endr
.endm

// emit the 8 pixels of the chunk in r5, MSB pixel first (or last when mirrored)
.macro write_sprite_column bpp double rtl
.if \rtl
.set pixel_shift, 0
.else
.set pixel_shift, 7*\bpp
.endif
.rept 8
  write_sprite_indexed pixel_shift \bpp \double
.if \rtl
.set pixel_shift, pixel_shift+\bpp
.else
.set pixel_shift, pixel_shift-\bpp
.endif
.endr
.endm

// advance r2 to the next column and loop while columns remain
.macro spr_next rtl step loop id
.if \rtl
  subs r2, #\step   // move to previous column
.else
  adds r2, #\step   // move to next column
.endif
  subs ip, #1
  bpl \loop\id
  b enc_spr_exit\id
.endm

// one column of 8 pixels from the bitmap byte at r2
.macro write_sprite_bmp_column double rtl
  ldrb r3, [r2]   // load bitmap byte
.if \rtl
.rept 8
  write_sprite_bmp_rtl \double
.endr
.else
  lsls r3, #24    // move LSB to MSB
.rept 8
  write_sprite_bmp_ltr \double
.endr
.endif
.endm

// single and double width loops of 1bpp
.macro spr_bmp_loops rtl id
  tst r5, #SPRITE_MASK_DOUBLE_WIDTH
  beq spr_bmp_single\id

spr_bmp_double\id:
  write_sprite_bmp_column 1 \rtl
  spr_next \rtl 1 spr_bmp_double \id
spr_bmp_single\id:
  write_sprite_bmp_column 0 \rtl
  spr_next \rtl 1 spr_bmp_single \id
.endm

// single and double width loops of one indexed depth
.macro spr_indexed_loops bpp rtl id
  tst r5, #SPRITE_MASK_DOUBLE_WIDTH
  beq spr_\bpp\()bpp_single\id

spr_\bpp\()bpp_double\id:
  load_sprite_chunk \bpp
  write_sprite_column \bpp 1 \rtl
  spr_next \rtl \bpp spr_\bpp\()bpp_double \id
spr_\bpp\()bpp_single\id:
  load_sprite_chunk \bpp
  write_sprite_column \bpp 0 \rtl
  spr_next \rtl \bpp spr_\bpp\()bpp_single \id
.endm

// r0 is output RGB buffer
// r1 is sprite descriptor address
// r2 is sprite bitmap data (first byte of the first column drawn)
.macro encode_sprite rtl
  push {r4-r7, r9, lr}

  ldrh r6, [r1, #SPRITE_POS_X_OFFS]
  sxth r6, r6   // sign-extend the 16-bit value
  cmp r6, #768  // check if past RGB buffer
  bge enc_spr_exit_\@
  cmp r6, #-SPRITE_MAX_WIDTH*8*2  // check if visible (sprite max width * 8x doubled pixels)
  blt enc_spr_exit_\@

  ldr r9, =cgia_rgb_palette
  ldr r3, =sprite_colors

  lsls r6, #2   // 4 bytes per pixel (one word of 2px)
  adds r0, r6   // move RGB buffer pointer to correct position in line

  ldrb r5, [r1, #SPRITE_FLAGS_OFFS]
  ands r6, r5, #SPRITE_MASK_WIDTH
  mov ip, r6    // iterate this many columns, plus one

  ands r1, r5, #SPRITE_MASK_PIXEL_BITS
  beq spr_bmp_\@
  cmp r1, #SPRITE_BITS_2BPP
  beq spr_2bpp_\@
  cmp r1, #SPRITE_BITS_3BPP
  beq spr_3bpp_\@

spr_4bpp_\@:
  spr_indexed_loops 4 \rtl _\@
spr_3bpp_\@:
  spr_indexed_loops 3 \rtl _\@
spr_2bpp_\@:
  spr_indexed_loops 2 \rtl _\@

// ----- 1bpp sprites -----
spr_bmp_\@:
  ldrb r4, [r3, #1]         // sprite palette entry 1
  ldr r4, [r9, r4, LSL #2]  // RGB
  spr_bmp_loops \rtl _\@

enc_spr_exit_\@:
  pop {r4-r7, r9, pc}
.endm

decl_func cgia_encode_sprite
  encode_sprite 0

decl_func cgia_encode_sprite_mirror
  encode_sprite 1
