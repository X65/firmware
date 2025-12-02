#include "hardware/regs/addressmap.h"

.syntax unified
.cpu cortex-m33
.thumb

.section .bss, "aw", %nobits
.align 4
sprite_colors:
  .space 4*4  // Reserve 4 words for the sprites color array


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
#define SPRITE_COLOR_1_OFFS 8
#define SPRITE_COLOR_2_OFFS 9
#define SPRITE_COLOR_3_OFFS 10

#define CGIA_SPRITES     8
#define SPRITE_MAX_WIDTH 8

#define SPRITE_MASK_WIDTH        0b00000111
#define SPRITE_MASK_DOUBLE_WIDTH 0b00010000
#define SPRITE_MASK_MULTICOLOR   0b00100000
#define SPRITE_MASK_MIRROR_X     0b01000000
#define SPRITE_MASK_MIRROR_Y     0b10000000

// r0 is output RGB buffer
// r1 is sprite color indices array
// r2 is byte counter - do not touch!
// r3 is bitmap (8 LSB)
// r9 is palette begin address

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

// changes r0,r4
.macro write_sprite_multi sh_amt double
  mov r4, r3
  lsrs r4, #\sh_amt
  ands r4, 3        // 00000011 mask
  beq 1f            // do not set 00 pixel
  ldr r4, [r9, r4, LSL #2]  // load sprite color entry
  stmia r0!, {r4}
.if \double
  stmia r0!, {r4}
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

.macro spr_next rtl loop id
.if \rtl
  subs r2, #1   // move to previous bitmap byte
.else
  adds r2, #1   // move to next bitmap byte
.endif
  subs ip, #1
  bpl \loop\id
  b enc_spr_exit\id
.endm

// r0 is output RGB buffer
// r1 is sprite descriptor address
// r2 is sprite bitmap data
// r3 is sprite width
.macro encode_sprite rtl
  push {r4-r7, lr}
  mov r4, r8
  mov r5, r9
  mov r6, r10
  push {r4-r6}

  ldrh r6, [r1, #SPRITE_POS_X_OFFS]
  sxth r6, r6   // sign-extend the 16-bit value
  cmp r6, 768   // check if past RGB buffer
  bge enc_spr_exit_\@
  cmp r6, -SPRITE_MAX_WIDTH*8*2  // check if visible (sprite max width * 8x doubled pixels)
  blt enc_spr_exit_\@

  ldr r9, =cgia_rgb_palette

  lsls r6, #2   // 4 bytes per pixel (one word of 2px)
  adds r0, r6   // move RGB buffer pointer to correct position in line

  mov ip, r3    // iterate this times

  ldrb r5, [r1, #SPRITE_FLAGS_OFFS]

  tst r5, SPRITE_MASK_MULTICOLOR
  beq spr_bmp_\@

// ----- multicolor sprites -----
spr_multi_\@:
  ldrb r4, [r1, SPRITE_COLOR_1_OFFS]    // load sprite color 1
  ldr r6, [r9, r4, LSL #2]              // load palette entry
  ldrb r4, [r1, SPRITE_COLOR_2_OFFS]    // load sprite color 2
  ldr r7, [r9, r4, LSL #2]              // load palette entry
  ldrb r4, [r1, SPRITE_COLOR_3_OFFS]    // load sprite color 3
  ldr r8, [r9, r4, LSL #2]              // load palette entry
  ldr r9, =sprite_colors
  stmia r9, {r5-r8}   // entry 00 is not used, so just dummy write r5

.if \rtl
// multi color sprite right-to-left
spr_multi_rtl_\@:
  tst r5, SPRITE_MASK_DOUBLE_WIDTH
  beq spr_multi_rtl_single_\@

spr_multi_rtl_double_\@:
  ldrb r3, [r2]   // load bitmap byte
  write_sprite_multi 0 1
  write_sprite_multi 2 1
  write_sprite_multi 4 1
  write_sprite_multi 6 1
  spr_next \rtl spr_multi_rtl_double _\@
spr_multi_rtl_single_\@:
  ldrb r3, [r2]   // load bitmap byte
  write_sprite_multi 0 0
  write_sprite_multi 2 0
  write_sprite_multi 4 0
  write_sprite_multi 6 0
  spr_next \rtl spr_multi_rtl_single _\@

.else
// multi color sprite left-to-right
spr_multi_ltr_\@:
  tst r5, SPRITE_MASK_DOUBLE_WIDTH
  beq spr_multi_ltr_single_\@

spr_multi_ltr_double_\@:
  ldrb r3, [r2]   // load bitmap byte
  write_sprite_multi 6 1
  write_sprite_multi 4 1
  write_sprite_multi 2 1
  write_sprite_multi 0 1
  spr_next \rtl spr_multi_ltr_double _\@
spr_multi_ltr_single_\@:
  ldrb r3, [r2]   // load bitmap byte
  write_sprite_multi 6 0
  write_sprite_multi 4 0
  write_sprite_multi 2 0
  write_sprite_multi 0 0
  spr_next \rtl spr_multi_ltr_single _\@
.endif

// ----- bitmap (single color) sprites -----
spr_bmp_\@:
  ldrb r4, [r1, SPRITE_COLOR_1_OFFS]         // load sprite color 1
  ldr r4, [r9, r4, LSL #2]  // load palette entry

.if \rtl
// single color sprite right-to-left
spr_bmp_rtl_\@:
  tst r5, SPRITE_MASK_DOUBLE_WIDTH
  beq spr_bmp_rtl_single_\@

spr_bmp_rtl_double_\@:
  ldrb r3, [r2]   // load bitmap byte
  write_sprite_bmp_rtl 1
  write_sprite_bmp_rtl 1
  write_sprite_bmp_rtl 1
  write_sprite_bmp_rtl 1
  write_sprite_bmp_rtl 1
  write_sprite_bmp_rtl 1
  write_sprite_bmp_rtl 1
  write_sprite_bmp_rtl 1
  spr_next \rtl spr_bmp_rtl_double _\@
spr_bmp_rtl_single_\@:
  ldrb r3, [r2]   // load bitmap byte
  write_sprite_bmp_rtl 0
  write_sprite_bmp_rtl 0
  write_sprite_bmp_rtl 0
  write_sprite_bmp_rtl 0
  write_sprite_bmp_rtl 0
  write_sprite_bmp_rtl 0
  write_sprite_bmp_rtl 0
  write_sprite_bmp_rtl 0
  spr_next \rtl spr_bmp_rtl_single _\@

.else
// single color sprite left-to-right
spr_bmp_ltr_\@:
  tst r5, SPRITE_MASK_DOUBLE_WIDTH
  beq spr_bmp_ltr_single_\@

spr_bmp_ltr_double_\@:
  ldrb r3, [r2]   // load bitmap byte
  lsls r3, #24    // move LSB to MSB
  write_sprite_bmp_ltr 1
  write_sprite_bmp_ltr 1
  write_sprite_bmp_ltr 1
  write_sprite_bmp_ltr 1
  write_sprite_bmp_ltr 1
  write_sprite_bmp_ltr 1
  write_sprite_bmp_ltr 1
  write_sprite_bmp_ltr 1
  spr_next \rtl spr_bmp_ltr_double _\@
spr_bmp_ltr_single_\@:
  ldrb r3, [r2]   // load bitmap byte
  lsls r3, #24    // move LSB to MSB
  write_sprite_bmp_ltr 0
  write_sprite_bmp_ltr 0
  write_sprite_bmp_ltr 0
  write_sprite_bmp_ltr 0
  write_sprite_bmp_ltr 0
  write_sprite_bmp_ltr 0
  write_sprite_bmp_ltr 0
  write_sprite_bmp_ltr 0
  spr_next \rtl spr_bmp_ltr_single _\@
.endif

enc_spr_exit_\@:
  pop {r4-r6}
  mov r8, r4
  mov r9, r5
  mov r10, r6
  pop {r4-r7, pc}
.endm

decl_func cgia_encode_sprite
  encode_sprite 0

decl_func cgia_encode_sprite_mirror
  encode_sprite 1
