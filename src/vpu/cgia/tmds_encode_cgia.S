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
.cpu cortex-m0plus
.thumb

// Put every function in its own ELF section, to permit linker GC
.macro decl_func name
.section .time_critical.\name, "ax"
.global \name
.type \name,%function
.thumb_func
\name:
.endm

//////////////////// MODE 3 ////////////////////

// r0 is output TMDS buffer
// r1 is scanline data buffer - do not touch!
// r5 is bitmap (8 MSB)
// r6 is foreground color index
// r7 is background color index
// r8 is TMDS buffer length (1 channel)
// r9 is palette begin address
// changes r0,r2-r4,r5,r11
.macro write_pixel_bmp
  lsls r5, #1        // shift-out 1 bit
  bcc 1f
    // bit is 1 - draw foreground pixel
  mov r2, r6
  b 2f
1:  // bit is 0 - draw background pixel
  mov r2, r7
2:
  lsls r2, #4       // 4 channels per palette entry * 4 bytes per word
  add r2, r9        // r2 is used palettte entry address

  ldmia r2, {r2-r4}
  mov r11, r0
  stmia r0!, {r2}
  mov r2, r11
  add r2, r8
  str r3, [r2]
  add r2, r8
  str r4, [r2]
.endm

// r0 is output TMDS buffer
// r1 is scanline data buffer
// r2 is width in bytes
.macro tmds_encode_mode_3_shared_or_mapped mapped
  push {r4-r7, lr}
  mov r4, r8
  mov r5, r9
  mov r6, r10
  push {r4-r6}

  mov r6, r2
  lsls r6, #3       // 2 words: bitmap, color
.if \mapped
  lsls r2, #2
  add r6, r2        // and 1 word of background
.endif
  add r6, r1        // compute end scanline offset
  mov ip, r6        // will loop until reached

  ldr r6, =tmds_buffer_length
  ldr r6, [r6]
  mov r8, r6        // r8 - tmds buffer length (one color channel)

  ldr r6, =cgia_palette
  mov r9, r6        // r9 - palette begin address

3:
.if \mapped
  ldmia r1!, {r5-r7}  // load 3 words from scanline buffer
.else
  ldmia r1!, {r5-r6}  // load 2 words from scanline buffer
  ldr r7, =background_color
  ldrb r7, [r7]       // and background color memory register
.endif

  lsls r5, #24        // move LSB to MSB

  write_pixel_bmp
  write_pixel_bmp
  write_pixel_bmp
  write_pixel_bmp
  write_pixel_bmp
  write_pixel_bmp
  write_pixel_bmp
  write_pixel_bmp

  cmp r1, ip
  blo 3b

  pop {r4-r6}
  mov r8, r4
  mov r9, r5
  mov r10, r6
  pop {r4-r7, pc}
.endm

decl_func tmds_encode_mode_3_shared
	tmds_encode_mode_3_shared_or_mapped 0

decl_func tmds_encode_mode_3_mapped
	tmds_encode_mode_3_shared_or_mapped 1

//////////////////// MODE 5 ////////////////////

// r0 is output TMDS buffer
// r1 is scanline data buffer - do not touch!
// r5 is bitmap (8 MSB)
// r6 is foreground color index
// r7 is background color index
// r8 is TMDS buffer length (1 channel)
// r9 is palette begin address
// changes r0,r2-r4,r5,r11
.macro write_pixel_multi
  lsls r5, #1        // shift-out 1 bit
  bcc 1f
    // bit is 1 - draw foreground pixel
  mov r2, r6
  b 2f
1:  // bit is 0 - draw background pixel
  mov r2, r7
2:
  lsls r2, #4       // 4 channels per palette entry * 4 bytes per word
  add r2, r9        // r2 is used palettte entry address

  ldmia r2, {r2-r4}
  mov r11, r0
  stmia r0!, {r2}
  mov r2, r11
  add r2, r8
  str r3, [r2]
  add r2, r8
  str r4, [r2]
.endm

// r0 is output TMDS buffer
// r1 is scanline data buffer
// r2 is width in bytes
.macro tmds_encode_mode_5_shared_or_mapped mapped
  push {r4-r7, lr}
  mov r4, r8
  mov r5, r9
  mov r6, r10
  push {r4-r6}

  mov r6, r2
  lsls r6, #3       // 2 words: bitmap, color
.if \mapped
  lsls r2, #2
  add r6, r2        // and 1 word of background
.endif
  add r6, r1        // compute end scanline offset
  mov ip, r6        // will loop until reached

  ldr r6, =tmds_buffer_length
  ldr r6, [r6]
  mov r8, r6        // r8 - tmds buffer length (one color channel)

  ldr r6, =cgia_palette
  mov r9, r6        // r9 - palette begin address

3:
.if \mapped
  ldmia r1!, {r5-r7}  // load 3 words from scanline buffer
.else
  ldmia r1!, {r5-r6}  // load 2 words from scanline buffer
  ldr r7, =background_color
  ldrb r7, [r7]       // and background color memory register
.endif

  lsls r5, #24        // move LSB to MSB

  write_pixel_multi
  write_pixel_multi
  write_pixel_multi
  write_pixel_multi
  write_pixel_multi
  write_pixel_multi
  write_pixel_multi
  write_pixel_multi

  cmp r1, ip
  blo 3b

  pop {r4-r6}
  mov r8, r4
  mov r9, r5
  mov r10, r6
  pop {r4-r7, pc}
.endm

decl_func tmds_encode_mode_5_shared
	tmds_encode_mode_5_shared_or_mapped 0

decl_func tmds_encode_mode_5_mapped
	tmds_encode_mode_5_shared_or_mapped 1

//////////////////// BORDER ////////////////////

.macro push_pixels
  ldmia r1!, {r4}   // load palette entry and move to next colour channel
  mov r5, r4        // duplicate pixel channel symbol x4
  mov r6, r4
  mov r7, r4
1:
  stmia r0!, {r4-r7}  // write 4 pixels of blue channel (8 real pixels)
  stmia r0!, {r4-r7}  // and another, which gives 8 total
  subs r2, #1
  bne 1b
.endm

// r0 is output TMDS buffer
// r1 is background colour
// r2 is width in columns
decl_func tmds_encode_border
  push {r4-r7, lr}
  mov r4, r8
  mov r5, r9
  mov r6, r10
  push {r4-r6}

  ldr r6, =tmds_buffer_length
  ldr r6, [r6]
  mov r8, r6      // keep for later use
  mov r9, r2      // save column counter for later

  ldr r4, =cgia_palette
  lsls r1, #4     // 4 channels per palette entry * 4 bytes per word
  adds r1, r4     // r1 is background colour palette entry

  mov r10, r0     // save base for next channel
  push_pixels     // push blue channel
  mov ip, r0      // save TMDS buffer offset for next iteration or return
  mov r2, r9      // restore column counter
  add r10, r8     // move by buffer_length
  mov r0, r10     // set base for next channel
  push_pixels     // push green channel
  mov r2, r9      // restore column counter
  add r10, r8     // move by buffer_length
  mov r0, r10     // set base for next channel
  push_pixels     // push green channel

  mov r0, ip      // restore saved output buffer pointer

  pop {r4-r6}
  mov r8, r4
  mov r9, r5
  mov r10, r6
  pop {r4-r7, pc}

.align 2
tmds_buffer_length:
.word 768*2       // 768 px - 2px per word (4/2)


//////////////////// SCANLINE BUFFER ////////////////////

// r0 - destination scanline buffer
// r1 - how many columns to load
.macro load_scanline_buffer_shared_or_mapped mapped
  push {r4-r7, lr}

  ldr r2, =(SIO_BASE + SIO_INTERP0_ACCUM0_OFFSET)
  movs r3, #127

1:
.if \mapped
  ldr r6, [r2, #PEEK1_OFFS + INTERP1]   // load background color pointer
  ldrb r6, [r6]     // load background color value (index)
  ands r6, r3       // clamp index to 127
.endif

  ldr r5, [r2, #POP0_OFFS + INTERP1]    // load color pointer
  ldrb r5, [r5]     // load background color value (index)
  ands r5, r3       // clamp index to 127

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
