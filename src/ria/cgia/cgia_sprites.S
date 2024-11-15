#include "hardware/regs/addressmap.h"

.syntax unified
.cpu cortex-m33
.thumb

.data

// Put every function in its own ELF section, to permit linker GC
.macro decl_func name
.section .time_critical.\name, "ax"
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
#define SPRITE_COLORS_OFFS  6   // COLOR 0 is not used, so point one byte before color table
#define SPRITE_COLOR_1_OFFS 7
#define SPRITE_COLOR_2_OFFS 8
#define SPRITE_COLOR_3_OFFS 9

#define SPRITE_MASK_WIDTH        0b00000011
#define SPRITE_MASK_MULTICOLOR   0b00001000
#define SPRITE_MASK_DOUBLE_WIDTH 0b00010000
#define SPRITE_MASK_MIRROR_X     0b00100000
#define SPRITE_MASK_MIRROR_Y     0b01000000
#define SPRITE_MASK_ACTIVE       0b10000000

// r0 is output RGB buffer
// r1 is sprite color indices array
// r2 is byte counter - do not touch!
// r3 is bitmap (8 LSB)
// r9 is palette begin address

// changes r0
.macro write_sprite_bmp
  // need bit shift/test first
  bcc 1f
  // bit is 1 - draw foreground pixel
  stmia r0!, {r4}
  tst r5, SPRITE_MASK_DOUBLE_WIDTH
  beq 2f
  stmia r0!, {r4}
  b 2f
1:  // bit is 0 - draw background pixel
  adds r0, #4     // move 1 screen pixel
  tst r5, SPRITE_MASK_DOUBLE_WIDTH
  beq 2f
  adds r0, #4     // move second screen pixel
2:
.endm

// changes r0,r3
.macro write_sprite_bmp_ltr
  lsls r3, #1        // shift-out 1 left bit
  write_sprite_bmp
.endm

// changes r0,r3
.macro write_sprite_bmp_rtl
  lsrs r3, #1        // shift-out right 1 bit
  write_sprite_bmp
.endm

// changes r0,r4
.macro write_sprite_multi sh_amt
  mov r4, r3
  lsrs r4, #\sh_amt
  ands r4, 3        // 00000011 mask
  beq 1f            // do not set 00 pixel
  ldrb r4, [r1, r4] // load sprite color
  ldr r4, [r9, r4, LSL #2]  // load palette entry
  stmia r0!, {r4}
  tst r5, SPRITE_MASK_DOUBLE_WIDTH
  beq 2f
  stmia r0!, {r4}
  b 2f
1:
  adds r0, #4     // move 1 screen pixel
  tst r5, SPRITE_MASK_DOUBLE_WIDTH
  beq 2f
  adds r0, #4     // move second screen pixel
2:
.endm

// r0 is output RGB buffer
// r1 is sprite descriptor address
// r2 is sprite bitmap data
// r3 is sprite width
decl_func cgia_encode_sprite
  push {r4-r7, lr}
  mov r4, r8
  mov r5, r9
  mov r6, r10
  push {r4-r6}

  ldr r9, =cgia_rgb_palette

  ldrh r6, [r1, #SPRITE_POS_X_OFFS]
  sxth r6, r6     //sign-extend the 16-bit value
  cmp r6, 768   // check if past RGB buffer
  bge 6f
  cmp r6, -4*8*2  // check if visible (sprite max width is 4x 8 doubled pixels)
  blt 6f

  lsls r6, #2   // 4 bytes per pixel (one word of 2px)
  adds r0, r6   // move RGB buffer pointer to correct position in line

  adds r1, #SPRITE_COLORS_OFFS    // move r1 to point to flags/color data
  mov ip, r3  // iterate this times

5:
  ldrb r3, [r2]  // load bitmap byte

  ldrb r5, [r1]
  tst r5, SPRITE_MASK_MULTICOLOR
  beq spr_bmp

  tst r5, SPRITE_MASK_MIRROR_X
  beq spr_multi

  // multicolor sprite mirrored X
  write_sprite_multi 0
  write_sprite_multi 2
  write_sprite_multi 4
  write_sprite_multi 6
  b spr_next

spr_multi:
  write_sprite_multi 6
  write_sprite_multi 4
  write_sprite_multi 2
  write_sprite_multi 0
  b spr_next

spr_bmp:
  ldrb r4, [r1, #1] // load sprite color 1
  ldr r4, [r9, r4, LSL #2]  // load palette entry

  tst r5, SPRITE_MASK_MIRROR_X
  beq spr_bmp_ltr
  // single color sprite right-to-left
  write_sprite_bmp_rtl
  write_sprite_bmp_rtl
  write_sprite_bmp_rtl
  write_sprite_bmp_rtl
  write_sprite_bmp_rtl
  write_sprite_bmp_rtl
  write_sprite_bmp_rtl
  write_sprite_bmp_rtl
  b spr_next

spr_bmp_ltr:
  lsls r3, #24        // move LSB to MSB
  write_sprite_bmp_ltr
  write_sprite_bmp_ltr
  write_sprite_bmp_ltr
  write_sprite_bmp_ltr
  write_sprite_bmp_ltr
  write_sprite_bmp_ltr
  write_sprite_bmp_ltr
  write_sprite_bmp_ltr

spr_next:
  adds r2, #1   // move to next bitmap byte
  subs ip, #1
  bpl 5b

6:
  pop {r4-r6}
  mov r8, r4
  mov r9, r5
  mov r10, r6
  pop {r4-r7, pc}
