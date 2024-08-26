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

.data
cell_colors: .skip 4  // Reserve 4 bytes for the multi color array


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
  ldr r7, =shared_color
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
// r1 is 00000011 mask
// r2 is cell_colors array pointer
// r3 is bitmap (8 LSB)
// r8 is TMDS buffer length (1 channel)
// r9 is palette begin address
// changes r0,r4-r6,r7
.macro write_pixel_multi sh_amt
  mov r4, r3
  lsrs r4, #\sh_amt
  ands r4, r1
  ldrb r4, [r2, r4]
  lsls r4, #4       // 4 channels per palette entry * 4 bytes per word
  add r4, r9        // r4 is used palettte entry address

  ldmia r4, {r4-r6}
  mov r7, r0
  stmia r0!, {r4}
  add r7, r8
  str r5, [r7]
  add r7, r8
  str r6, [r7]
.endm

// changes r0,r4-r6,r7,r11
.macro write_pixel_multi_doubled sh_amt
  mov r4, r3
  lsrs r4, #\sh_amt
  ands r4, r1
  ldrb r5, [r2, r4]
  lsls r5, #4       // 4 channels per palette entry * 4 bytes per word
  add r5, r9        // r4 is used palettte entry address

  ldmia r5, {r5-r7}
  mov r4, r5
  mov r11, r0
  stmia r0!, {r4-r5}
  add r11, r8
  mov r5, r6
  mov r4, r11
  stmia r4!, {r5-r6}
  add r11, r8
  mov r6, r7
  mov r4, r11
  stmia r4!, {r6-r7}
.endm

// r0 is output TMDS buffer
// r1 is scanline data buffer
// r2 is width in bytes
.macro tmds_encode_mode_5_or_7 doubled
  push {r4-r7, lr}
  mov r4, r8
  mov r5, r9
  mov r6, r10
  push {r4-r6}

  mov r6, r2
  lsls r6, #3       // 2 words: bitmap, color
  lsls r2, #2
  add r6, r2        // and 1 word of background
  add r6, r1        // compute end scanline offset
  mov ip, r6        // will loop until reached

  ldr r6, =tmds_buffer_length
  ldr r6, [r6]
  mov r8, r6        // r8 - tmds buffer length (one color channel)

  ldr r6, =cgia_palette
  mov r9, r6        // r9 - palette begin address

  ldr r2, =cell_colors
  ldr r6, =shared_color // load shared background color memory registers
  ldrh r6, [r6]
  strb r6, [r2, #0]   // shared color
  lsrs r6, #8
  strb r6, [r2, #3]   // TODO: you cannot change the value during rendering (by CPU write to register)
                      // NOTE: see how MODE3 does this
3:
  ldmia r1!, {r3-r5}  // load 3 words from scanline buffer
  strb r5, [r2, #1]   // store "off" color
  strb r4, [r2, #2]   // store "on" color

  mov r10, r1 // backup buffer pointer
  movs r1, 3  // 00000011
.if \doubled
  write_pixel_multi_doubled 6
  write_pixel_multi_doubled 4
  write_pixel_multi_doubled 2
  write_pixel_multi_doubled 0
.else
  write_pixel_multi 6
  write_pixel_multi 4
  write_pixel_multi 2
  write_pixel_multi 0
.endif
  mov r1, r10 // restore buffer pointer

  cmp r1, ip
  blo 3b

  pop {r4-r6}
  mov r8, r4
  mov r9, r5
  mov r10, r6
  pop {r4-r7, pc}
.endm

decl_func tmds_encode_mode_5
	tmds_encode_mode_5_or_7 0

decl_func tmds_encode_mode_7
	tmds_encode_mode_5_or_7 1

//////////////////// SPRITES ////////////////////

#define SPRITE_POS_X_OFFS   0
#define SPRITE_POS_Y_OFFS   2
#define SPRITE_LINES_Y_OFFS 3
#define SPRITE_FLAGS_OFFS   4
#define SPRITE_COLORS_OFFS  4   // COLOR 0 is not used, so point one byte before color table
#define SPRITE_COLOR_1_OFFS 5
#define SPRITE_COLOR_2_OFFS 6
#define SPRITE_COLOR_3_OFFS 7
#define SPRITE_DATA_OFFS    12  // data copied from CPU memory by PIO+DMA

#define SPRITE_MASK_WIDTH        0b00000011
#define SPRITE_MASK_ACTIVE       0b00000100
#define SPRITE_MASK_MULTICOLOR   0b00001000
#define SPRITE_MASK_DOUBLE_WIDTH 0b00010000

// r0 is output TMDS buffer
// r1 is sprite color indices array
// r2 is byte counter - do not touch!
// r3 is bitmap (8 LSB)
// r7 is 00000011 mask
// r8 is TMDS buffer length (1 channel)
// r9 is palette begin address
// r10 is max allowed TMDS address
// changes r0,r4-r6
.macro write_sprite_multi sh_amt
  mov r4, r3
  lsrs r4, #\sh_amt
  ands r4, r7
  beq 1f            // do not set 00 pixel
  ldrb r4, [r1, r4]
  lsls r4, #4       // 4 channels per palette entry * 4 bytes per word
  add r4, r9        // r4 is used palettte entry address

  ldmia r4, {r4-r6}
  str r4, [r0]
  mov r4, r0
  add r4, r8
  str r5, [r4]
  add r4, r8
  str r6, [r4]
1:
  adds r0, #4     // move 1 screen pixel
.endm

// r0 is output TMDS buffer
// r1 is sprite descriptor address
// r2 is sprite width
decl_func tmds_encode_sprite
  push {r4-r7, lr}
  mov r4, r8
  mov r5, r9
  mov r6, r10
  push {r4-r6}

  ldr r6, =tmds_buffer_length
  ldr r6, [r6]
  mov r8, r6        // r8 - tmds buffer length (one color channel)
  mov r10, r6
  add r10, r0       // r10 - max allowed TMDS address

  ldr r6, =cgia_palette
  mov r9, r6        // r9 - palette begin address

  movs r7, 3        // shift mask: 00000011

  ldrh r6, [r1, #SPRITE_POS_X_OFFS]
  lsls r6, #2   // 4 bytes per pixel (one word of 2px)
  adds r0, r6   // move TMDS buffer pointer to correct position in line

  cmp r0, r10   // check if past TMDS buffer
  bge 4f

  // TODO: implement flags handling

  adds r1, #SPRITE_COLORS_OFFS    // move r1 to point to color data

  adds r2, (SPRITE_DATA_OFFS - SPRITE_COLORS_OFFS)
  mov ip, r2  // iterate up untill this index
  movs r2, (SPRITE_DATA_OFFS - SPRITE_COLORS_OFFS)

3:
  ldrb r3, [r1, r2]  // load bitmap byte

  write_sprite_multi 6
  cmp r0, r10   // check if past TMDS buffer
  bge 4f
  write_sprite_multi 4
  cmp r0, r10   // check if past TMDS buffer
  bge 4f
  write_sprite_multi 2
  cmp r0, r10   // check if past TMDS buffer
  bge 4f
  write_sprite_multi 0
  cmp r0, r10   // check if past TMDS buffer
  bge 4f

  adds r2, #1
  cmp r2, ip
  ble 3b

4:
  pop {r4-r6}
  mov r8, r4
  mov r9, r5
  mov r10, r6
  pop {r4-r7, pc}


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
