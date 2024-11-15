#include "hardware/regs/addressmap.h"
#include "hardware/regs/sio.h"

// Offsets suitable for ldr/str (must be <= 0x7c):
#define ACCUM0_OFFS     (SIO_INTERP0_ACCUM0_OFFSET     - SIO_INTERP0_ACCUM0_OFFSET)
#define ACCUM1_OFFS     (SIO_INTERP0_ACCUM1_OFFSET     - SIO_INTERP0_ACCUM0_OFFSET)
#define PEEK0_OFFS      (SIO_INTERP0_PEEK_LANE0_OFFSET - SIO_INTERP0_ACCUM0_OFFSET)
#define PEEK1_OFFS      (SIO_INTERP0_PEEK_LANE1_OFFSET - SIO_INTERP0_ACCUM0_OFFSET)
#define PEEK2_OFFS      (SIO_INTERP0_PEEK_FULL_OFFSET  - SIO_INTERP0_ACCUM0_OFFSET)
#define POP0_OFFS       (SIO_INTERP0_POP_LANE0_OFFSET  - SIO_INTERP0_ACCUM0_OFFSET)
#define POP1_OFFS       (SIO_INTERP0_POP_LANE1_OFFSET  - SIO_INTERP0_ACCUM0_OFFSET)
#define POP2_OFFS       (SIO_INTERP0_POP_FULL_OFFSET   - SIO_INTERP0_ACCUM0_OFFSET)
#define INTERP1         (SIO_INTERP1_ACCUM0_OFFSET     - SIO_INTERP0_ACCUM0_OFFSET)

.syntax unified
.cpu cortex-m33
.thumb

.data
cell_colors: .skip 4*4  // Reserve 4 words for the multi color array


// Put every function in its own ELF section, to permit linker GC
.macro decl_func name
.section .time_critical.\name, "ax"
.global \name
.type \name,%function
.thumb_func
\name:
.endm


//////////////////// MODE 3 ////////////////////

// r0 is output RGB buffer
// r1 is scanline data buffer - do not touch!
// r5 is bitmap (8 MSB)
// r6 is foreground color index
// r7 is background color index
// changes r0,r5
.macro write_pixel_bmp mapped
  lsls r5, #1        // shift-out 1 bit
  bcc 1f
    // bit is 1 - draw foreground pixel
  stmia r0!, {r6}
  b 2f
1:  // bit is 0 - draw background pixel
.if \mapped
  stmia r0!, {r7}
.else
  adds r0, 1  // transparent pixel - move to next
.endif
2:
.endm

// r0 is output RGB buffer
// r1 is scanline data buffer
// r2 is width in bytes
.macro cgia_encode_mode_3_shared_or_mapped mapped
  push {r4-r7, lr}

  ldr r3, =cgia_rgb_palette

3:
.if \mapped
  ldmia r1!, {r5-r7}  // load 3 words from scanline buffer
.else
  ldmia r1!, {r5-r6}  // load 2 words from scanline buffer
.endif

  lsls r5, #24        // move LSB to MSB
  ldr r6, [r3, r6, LSL #2]  // load palette entry
  ldr r7, [r3, r7, LSL #2]  // load palette entry

  write_pixel_bmp \mapped
  write_pixel_bmp \mapped
  write_pixel_bmp \mapped
  write_pixel_bmp \mapped
  write_pixel_bmp \mapped
  write_pixel_bmp \mapped
  write_pixel_bmp \mapped
  write_pixel_bmp \mapped

  subs r2, #1
  bne 3b

  pop {r4-r7, pc}
.endm

decl_func cgia_encode_mode_3_shared
	cgia_encode_mode_3_shared_or_mapped 0

decl_func cgia_encode_mode_3_mapped
	cgia_encode_mode_3_shared_or_mapped 1


//////////////////// MODE 5 ////////////////////

// r0 is output RGB buffer
// r4 is bitmap (8 LSB)
// r7 is cell_colors array pointer
// changes r0,r5-r6
.macro write_pixel_multi sh_amt doubled
  mov r5, r4
  lsrs r5, #\sh_amt
  ands r5, 3  // 00000011 mask
  ldr r5, [r7, r5, LSL #2]
.if \doubled
  mov r6, r5
  stmia r0!, {r5-r6}
.else
  stmia r0!, {r5}
.endif
.endm

// r0 is output RGB buffer
// r1 is scanline data buffer
// r2 is width in bytes
// r3 is shared_colors array [2]
.macro cgia_encode_mode_5_or_7 doubled
  push {r4-r7, lr}

  mov r4, r3  // shared_colors[0]
  mov r5, r3
  adds r5, 4  // shared_colors[1]

  ldr r3, =cgia_rgb_palette
  ldr r7, =cell_colors

  ldrb r4, [r4]
  ldr r4, [r3, r4, LSL #2]  // load palette entry
  str r4, [r7, #0]          // store shared color 0
  ldrb r5, [r5]
  ldr r5, [r3, r5, LSL #2]  // load palette entry
  str r5, [r7, #12]         // store shared color 3

3:
  ldmia r1!, {r4-r6}        // load 3 words from scanline buffer
  ldr r6, [r3, r6, LSL #2]  // load palette entry
  str r6, [r7, #4]          // store "off" color (1)
  ldr r5, [r3, r5, LSL #2]  // load palette entry
  str r5, [r7, #8]          // store "on" color (2)

.if \doubled
  write_pixel_multi 6 1
  write_pixel_multi 4 1
  write_pixel_multi 2 1
  write_pixel_multi 0 1
.else
  write_pixel_multi 6 0
  write_pixel_multi 4 0
  write_pixel_multi 2 0
  write_pixel_multi 0 0
.endif

  subs r2, #1
  bne 3b

  pop {r4-r7, pc}
.endm

decl_func cgia_encode_mode_5
	cgia_encode_mode_5_or_7 0

decl_func cgia_encode_mode_7
	cgia_encode_mode_5_or_7 1


//////////////////// BORDER ////////////////////

// r0 is output RGB buffer
// r1 is width in columns
// r2 is background colour
decl_func cgia_encode_border
  ldr r3, =cgia_rgb_palette
  ldr r3, [r3, r2, LSL #2]  // load palette entry
  mov r2, r3                // copy to r2

1:
  stmia r0!, {r2-r3}  // write two pixels and move to next
  stmia r0!, {r2-r3}  // 2 more
  stmia r0!, {r2-r3}  // 2 more
  stmia r0!, {r2-r3}  // 2 more => 8 pixels (per column in r1)
  subs r1, #1         // decrement count
  bne 1b              // repeat until count is zero

  mov pc, lr      // return with updated r0


//////////////////// SCANLINE BUFFER ////////////////////

// r0 - destination scanline buffer
// r1 - how many columns to load
.macro load_scanline_buffer_shared_or_mapped mapped
  push {r4-r7, lr}

  ldr r2, =(SIO_BASE + SIO_INTERP0_ACCUM0_OFFSET)

1:
.if \mapped
  ldr r6, [r2, #PEEK1_OFFS + INTERP1]   // load background color pointer
  ldrb r6, [r6]     // load background color value (index)
.endif

  ldr r5, [r2, #POP0_OFFS + INTERP1]    // load color pointer
  ldrb r5, [r5]     // load background color value (index)

  ldr r4, [r2, #POP0_OFFS]    // load bitmap offset
  ldrb r4, [r4]     // load bitmap data

.if \mapped
  stmia r0!, {r4-r6}
.else
  stmia r0!, {r4-r5}
.endif

  subs r1, #1
  bne 1b

  pop {r4-r7, pc}
.endm

decl_func load_scanline_buffer_shared
	load_scanline_buffer_shared_or_mapped 0

decl_func load_scanline_buffer_mapped
	load_scanline_buffer_shared_or_mapped 1


// r0 - destination scanline buffer
// r1 - how many columns to load
// r2 - character generator address
// r3 - character length in shift amount
decl_func load_textmode_buffer
  push {r4-r7, lr}

  mov ip, r2        // ip - chargen

  ldr r2, =(SIO_BASE + SIO_INTERP0_ACCUM0_OFFSET)

1:
  ldr r6, [r2, #PEEK1_OFFS + INTERP1]   // load background color pointer
  ldrb r6, [r6]     // load background color value (index)

  ldr r5, [r2, #POP0_OFFS + INTERP1]    // load color pointer
  ldrb r5, [r5]     // load background color value (index)

  ldr r4, [r2, #POP0_OFFS]    // load character code offset
  ldrb r4, [r4]     // load character code
  lsls r4, r3
  add r4, ip
  ldrb r4, [r4]     // load character line bitmap data

  stmia r0!, {r4-r6}

  subs r1, #1
  bne 1b

  pop {r4-r7, pc}
