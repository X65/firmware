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
// r1 is bitmap (8 MSB)
// r4 is foreground color index
// r8 is TMDS buffer length (1 channel)
// r9 is palette begin address
// r11 is background color index
// uses r1,r3,r5-r7
.macro write_pixel
  lsls r1, #1        // shift-out 1 bit
  bcc 1f
    // bit is 1 - draw foreground pixel
  mov r5, r4
  b 2f
1:  // bit is 0 - draw background pixel
  mov r5, r11
2:
  lsls r5, #4       // 4 channels per palette entry * 4 bytes per word
  add r5, r9        // r5 is used palettte entry address

  ldmia r5, {r5-r7}
  mov r3, r0
  stmia r0!, {r5}
  add r3, r8
  str r6, [r3]
  add r3, r8
  str r7, [r3]
.endm

// r0 is output TMDS buffer
// r1 is width in pixels
// r2 is background_color address
// interpolator 0 lane 0 is configured to index bitmap data
// interpolator 1 lane 0 is configured to index colour map
// interpolator 1 lane 0 is configured to index background map

.macro tmds_encode_mode_3_shared_or_mapped mapped
  push {r4-r7, lr}
  mov r4, r8
  mov r5, r9
  mov r6, r10
  push {r4-r6}

.if !\mapped
  mov r10, r2       // r10 - background color address
.endif

  lsls r1, #1       // 2 symbols per word -> 2 bytes per symbol
  mov ip, r1
  add ip, r0        // loop until reaching required buffer offset

  ldr r2, =(SIO_BASE + SIO_INTERP0_ACCUM0_OFFSET)

  ldr r6, =tmds_buffer_length
  ldr r6, [r6]
  mov r8, r6        // keep for later use

  ldr r6, =cgia_palette
  mov r9, r6        // r9 - palette begin address


3:
  movs r5, #127

.if \mapped
  ldr r4, [r2, #PEEK1_OFFS + INTERP1]  // load background color pointer
.else
  mov r4, r10       // shared background color (given in r1)
.endif
  ldrb r4, [r4]
  ands r4, r5       // clamp index to 127
  mov r11, r4       // r11 - background color index

  ldr r4, [r2, #POP0_OFFS + INTERP1]  // load color pointer
  ldrb r4, [r4]     // r4 - foreground color index
  ands r4, r5       // clamp index to 127

  ldr r1, [r2, #POP0_OFFS]  // load bitmap offset
  ldrb r1, [r1]     // load bitmap data
  lsls r1, #24      // move to MSB

  write_pixel
  write_pixel
  write_pixel
  write_pixel
  write_pixel
  write_pixel
  write_pixel
  write_pixel

  cmp r0, ip
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
