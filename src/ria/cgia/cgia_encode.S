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
.section .scratch_y.\name, "ax"
.global \name
.type \name,%function
.thumb_func
\name:
.endm


//////////////////// MODE 2 ////////////////////

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
  adds r0, 4  // transparent pixel - move to next
.endif
2:
.endm

// r0 is output RGB buffer
// r1 is width in bytes
// r2 - character generator address
// r3 - character length in shift amount
.macro cgia_encode_mode_2_shared_or_mapped mapped
  push {r4-r7, lr}

  ldr r4, =cgia_rgb_palette
  ldr ip, =(SIO_BASE + SIO_INTERP0_ACCUM0_OFFSET)

3:
.if \mapped
  ldr r7, [ip, #PEEK1_OFFS + INTERP1]   // load background color pointer
  ldrb r7, [r7]     // load background color value (index)
  ldr r7, [r4, r7, LSL #2]  // load palette entry
.endif
  ldr r6, [ip, #POP0_OFFS + INTERP1]    // load foreground color pointer
  ldrb r6, [r6]     // load foreground color value (index)
  ldr r6, [r4, r6, LSL #2]  // load palette entry

  ldr r5, [ip, #POP0_OFFS]    // load character code offset
  ldrb r5, [r5]     // load character code
  lsls r5, r3       // multiply by character height
  ldrb r5, [r2, r5]     // load character line bitmap data

  lsls r5, #24        // move LSB to MSB

  write_pixel_bmp \mapped
  write_pixel_bmp \mapped
  write_pixel_bmp \mapped
  write_pixel_bmp \mapped
  write_pixel_bmp \mapped
  write_pixel_bmp \mapped
  write_pixel_bmp \mapped
  write_pixel_bmp \mapped

  subs r1, #1
  bne 3b

  pop {r4-r7, pc}
.endm

decl_func cgia_encode_mode_2_shared
	cgia_encode_mode_2_shared_or_mapped 0

decl_func cgia_encode_mode_2_mapped
	cgia_encode_mode_2_shared_or_mapped 1

// r0 is output RGB buffer
// r1 is width in bytes
// r2 - character generator address
// r3 - character length in shift amount
decl_func cgia_encode_vt
  push {r4-r7, lr}

  ldr ip, =(SIO_BASE + SIO_INTERP0_ACCUM0_OFFSET)

3:
  ldr r5, [ip, #POP0_OFFS]    // load character code offset
  mov r6, r5
  mov r7, r5
  adds r6, 4        // move to fg color value
  ldr r6, [r6]      // load foreground color value
  adds r7, 8        // move to bg color value
  ldr r7, [r7]      // load background color value
  ldrb r5, [r5]     // load character code
  lsls r5, r3       // multiply by character height
  ldrb r5, [r2, r5] // load character line bitmap data

  lsls r5, #24      // move LSB to MSB

  write_pixel_bmp 1
  write_pixel_bmp 1
  write_pixel_bmp 1
  write_pixel_bmp 1
  write_pixel_bmp 1
  write_pixel_bmp 1
  write_pixel_bmp 1
  write_pixel_bmp 1

  subs r1, #1
  bne 3b

  pop {r4-r7, pc}


//////////////////// MODE 3 ////////////////////

// r0 is output RGB buffer
// r1 is width in bytes
.macro cgia_encode_mode_3_shared_or_mapped mapped
  push {r4-r7, lr}

  ldr r2, =(SIO_BASE + SIO_INTERP0_ACCUM0_OFFSET)
  ldr r3, =cgia_rgb_palette

3:
.if \mapped
  ldr r7, [r2, #PEEK1_OFFS + INTERP1]   // load background color pointer
  ldrb r7, [r7]     // load background color value (index)
  ldr r7, [r3, r7, LSL #2]  // load palette entry
.endif

  ldr r6, [r2, #POP0_OFFS + INTERP1]    // load foreground color pointer
  ldrb r6, [r6]     // load foreground color value (index)
  ldr r6, [r3, r6, LSL #2]  // load palette entry

  ldr r5, [r2, #POP0_OFFS]    // load bitmap offset
  ldrb r5, [r5]     // load bitmap data
  lsls r5, #24        // move LSB to MSB

  write_pixel_bmp \mapped
  write_pixel_bmp \mapped
  write_pixel_bmp \mapped
  write_pixel_bmp \mapped
  write_pixel_bmp \mapped
  write_pixel_bmp \mapped
  write_pixel_bmp \mapped
  write_pixel_bmp \mapped

  subs r1, #1
  bne 3b

  pop {r4-r7, pc}
.endm

decl_func cgia_encode_mode_3_shared
	cgia_encode_mode_3_shared_or_mapped 0

decl_func cgia_encode_mode_3_mapped
	cgia_encode_mode_3_shared_or_mapped 1


//////////////////// MODE 4 ////////////////////

// r0 is output RGB buffer
// r4 is bitmap (8 LSB)
// r7 is cell_colors array pointer
// changes r0,r5-r6
.macro write_pixel_multi sh_amt doubled mapped
  mov r5, r4
  lsrs r5, #\sh_amt
  ands r5, 3  // 00000011 mask
.if !\mapped
  beq 1f
.endif
  ldr r5, [r7, r5, LSL #2]
.if \doubled
  mov r6, r5
  stmia r0!, {r5-r6}
.else
  stmia r0!, {r5}
.endif
  b 2f
1:
.if \doubled
  adds r0, 8  // transparent pixel - skip 2
.else
  adds r0, 4  // transparent pixel - move to next
.endif
2:
.endm

// r0 is output RGB buffer
// r1 is width in bytes
// r2 - character generator address
// r3 - character length in shift amount
// First stack argument is shared_colors[2] array
.macro cgia_encode_mode_4 doubled mapped
  push {r4-r8, lr}

  ldr r8, =cgia_rgb_palette
  ldr r7, =cell_colors
  ldr ip, [sp, #24] // 6 words saved, so 24-byte offset to first stack argument

.if \mapped
  ldrb r4, [ip]   // shared_colors[0]
  ldr r4, [r8, r4, LSL #2]  // load palette entry
  str r4, [r7, #0]          // store shared color 0
.endif
  adds ip, 1  // shared_colors[1]
  ldrb r5, [ip]
  ldr r5, [r8, r5, LSL #2]  // load palette entry
  str r5, [r7, #12]         // store shared color 3

  ldr ip, =(SIO_BASE + SIO_INTERP0_ACCUM0_OFFSET)

3:
  ldr r6, [ip, #PEEK1_OFFS + INTERP1]   // load background color pointer
  ldrb r6, [r6]     // load background color value (index)
  ldr r6, [r8, r6, LSL #2]  // load palette entry
  str r6, [r7, #4]          // store "off" color (1)

  ldr r5, [ip, #POP0_OFFS + INTERP1]    // load foreground color pointer
  ldrb r5, [r5]     // load foreground color value (index)
  ldr r5, [r8, r5, LSL #2]  // load palette entry
  str r5, [r7, #8]          // store "on" color (2)

  ldr r4, [ip, #POP0_OFFS]    // load character code offset
  ldrb r4, [r4]     // load character code
  lsls r4, r3       // multiply by character height
  ldrb r4, [r2, r4]     // load character line bitmap data

.if \doubled
  write_pixel_multi 6 1 \mapped
  write_pixel_multi 4 1 \mapped
  write_pixel_multi 2 1 \mapped
  write_pixel_multi 0 1 \mapped
.else
  write_pixel_multi 6 0 \mapped
  write_pixel_multi 4 0 \mapped
  write_pixel_multi 2 0 \mapped
  write_pixel_multi 0 0 \mapped
.endif

  subs r1, #1
  bne 3b

  pop {r4-r8, pc}
.endm

decl_func cgia_encode_mode_4_shared
	cgia_encode_mode_4 0 0

decl_func cgia_encode_mode_4_mapped
	cgia_encode_mode_4 0 1

decl_func cgia_encode_mode_4_doubled_shared
	cgia_encode_mode_4 1 0

decl_func cgia_encode_mode_4_doubled_mapped
	cgia_encode_mode_4 1 1


//////////////////// MODE 5 ////////////////////

// r0 is output RGB buffer
// r1 is width in bytes
// r2 is shared_colors array [2]
.macro cgia_encode_mode_5 doubled mapped
  push {r4-r7, lr}

  ldr r3, =cgia_rgb_palette
  ldr r7, =cell_colors

.if \mapped
  ldrb r4, [r2]   // shared_colors[0]
  ldr r4, [r3, r4, LSL #2]  // load palette entry
  str r4, [r7, #0]          // store shared color 0
.endif
  adds r2, 1  // shared_colors[1]
  ldrb r5, [r2]
  ldr r5, [r3, r5, LSL #2]  // load palette entry
  str r5, [r7, #12]         // store shared color 3

  ldr r2, =(SIO_BASE + SIO_INTERP0_ACCUM0_OFFSET)

3:
  ldr r6, [r2, #PEEK1_OFFS + INTERP1]   // load background color pointer
  ldrb r6, [r6]     // load background color value (index)
  ldr r6, [r3, r6, LSL #2]  // load palette entry
  str r6, [r7, #4]          // store "off" color (1)

  ldr r5, [r2, #POP0_OFFS + INTERP1]    // load foreground color pointer
  ldrb r5, [r5]     // load foreground color value (index)
  ldr r5, [r3, r5, LSL #2]  // load palette entry
  str r5, [r7, #8]          // store "on" color (2)

  ldr r4, [r2, #POP0_OFFS]    // load bitmap offset
  ldrb r4, [r4]     // load bitmap data


.if \doubled
  write_pixel_multi 6 1 \mapped
  write_pixel_multi 4 1 \mapped
  write_pixel_multi 2 1 \mapped
  write_pixel_multi 0 1 \mapped
.else
  write_pixel_multi 6 0 \mapped
  write_pixel_multi 4 0 \mapped
  write_pixel_multi 2 0 \mapped
  write_pixel_multi 0 0 \mapped
.endif

  subs r1, #1
  bne 3b

  pop {r4-r7, pc}
.endm

decl_func cgia_encode_mode_5_shared
	cgia_encode_mode_5 0 0

decl_func cgia_encode_mode_5_mapped
	cgia_encode_mode_5 0 1

decl_func cgia_encode_mode_5_doubled_shared
	cgia_encode_mode_5 1 0

decl_func cgia_encode_mode_5_doubled_mapped
	cgia_encode_mode_5 1 1


//////////////////// MODE 6 ////////////////////

// r0 is output RGB buffer
// r2 is base_color array [8]
// r4 is cgia_rgb_palette
// r6 is HAM command
// changes r0,r7,r11,r12
ham_command:
  // split instruction
  lsrs r7, r6, #3  // extract code

  cmp r7, #0  // --- 000 - load base color
  bne 101f
  ands r6, #7               // 00000111 mask data
  ldrb r6, [r2, r6]         // load base color index
  ldr r12, [r4, r6, LSL #2] // load palette entry to current color
  b 111f
101:
  cmp r7, #1  // --- 001 - blend current color
  bne 102f
  ands r6, #7               // 00000111 mask data
  ldrb r6, [r2, r6]         // load base color index
  ldr r12, [r4, r6, LSL #2] // load palette entry to current color
                            // TODO: blend with r12
  b 111f
102:            // --- Modify a channel
  ands r6, #15  // 00001111 mask data
  lsls r6, #4   // Make changes in quanta of 16

  lsrs r7, #1   // extract two high bits of command

  cmp r7, #1  // --- 01x - Modify Red channel
  bne 103f
  ands r7, r12, #0xFF     // extract RED color value
  adds r7, r6             // add delta
  ands r7, #0xFF          // clamp to 8 bit
  ands r12, #0xFFFFFF00   // remove RED channel from current color
  orrs r12, r7            // merge modified value
  b 111f
103:
  cmp r7, #2  // --- 10x - Modify Green channel
  bne 104f
  ands r7, r12, #0xFF00   // extract GREEN color value
  adds r7, r7, r6, LSL #8 // add delta
  ands r7, #0xFF00        // clamp to 8 bit
  ands r12, #0xFFFF00FF   // remove GREEN channel from current color
  orrs r12, r7            // merge modified value
  b 111f
104:
              // --- 11x - Modify Blue channel
  ands r7, r12, #0xFF0000   // extract BLUE color value
  adds r7, r7, r6, LSL #16  // add delta
  ands r7, #0xFF0000        // clamp to 8 bit
  ands r12, #0xFF00FFFF     // remove BLUE channel from current color
  orrs r12, r7              // merge modified value
111:
  bx lr   // return

.macro write_pixel_ham doubled
.if \doubled
  mov r11, r12
  stmia r0!, {r11-r12}
.else
  stmia r0!, {r12}
.endif
.endm

// r0 is output RGB buffer
// r1 is width in bytes
// r2 is base_color array [8]
// r3 is background color
.macro _cgia_encode_mode_6 doubled
  push {r4-r7,r11, lr}

  ldr r4, =cgia_rgb_palette
  ldr r12, [r4, r3]  // get current color from background color

  ldr r3, =(SIO_BASE + SIO_INTERP0_ACCUM0_OFFSET)

3:
  ldr r5, [r3, #POP0_OFFS]  // load HAM command address
  ldr r5, [r5]              // load HAM command data
  // NOTICE: this is a full register load, which loads 4 commands at once
  // Since ARM is little endian, the in-register commands layout is magled,
  // thus we need some bit wrangling to unpack it
  // commands in register:
  // xxxxxxxx 22333333 11112222 00000011

  // extract first command
  mov r6, r5, LSR #2  // skip two lowest bits
  ands r6, #0x3F      // mask 6 command bits
  bl ham_command
  write_pixel_ham \doubled
  // extract second command
  mov r6, r5, LSR #12
  ands r6, #15        // get lower 4 bits from second byte
  ands r7, r5, #3     // get higher 2 bits from first byte
  lsls r7, #4         // move to correct position
  orrs r6, r7         // merge higher bits to r6
  bl ham_command
  write_pixel_ham \doubled
  // extract third command
  mov r6, r5, LSR #6
  ands r6, #60              // get higher 4 bits into bits 5-2
  ands r7, r5, #0xC00000    // extract highest 2 bits from third byte
  orrs r6, r6, r7, LSR #22  // merge highest two bits as lowest two bits
  bl ham_command
  write_pixel_ham \doubled
  // extract fourth command
  mov r6, r5, LSR #16 // get third byte
  ands r6, #0x3F      // mask 6 command bits
  bl ham_command
  write_pixel_ham \doubled

  ldr r5, [r3, #POP0_OFFS]  // pop two more addr
  ldr r5, [r3, #POP0_OFFS]  // we've already processed these
  subs r1, #1
  bne 3b

  pop {r4-r7,r11, pc}
.endm

decl_func cgia_encode_mode_6
	_cgia_encode_mode_6 0

decl_func cgia_encode_mode_6_doubled
	_cgia_encode_mode_6 1


//////////////////// MODE 7 ////////////////////

// r0 is output RGB buffer
// r1 is width in bytes
decl_func cgia_encode_mode_7
  push {r4-r7, lr}

  ldr r2, =(SIO_BASE + SIO_INTERP0_ACCUM0_OFFSET)
  ldr r3, =cgia_rgb_palette

1:
  ldr r4, [r2, #POP2_OFFS]  // load texture address
  ldrb r4, [r4]             // load color index
  ldr r4, [r3, r4, LSL #2]  // load palette entry
  ldr r5, [r2, #POP2_OFFS]
  ldrb r5, [r5]
  ldr r5, [r3, r5, LSL #2]
  ldr r6, [r2, #POP2_OFFS]
  ldrb r6, [r6]
  ldr r6, [r3, r6, LSL #2]
  ldr r7, [r2, #POP2_OFFS]
  ldrb r7, [r7]
  ldr r7, [r3, r7, LSL #2]
  stmia r0!, {r4-r7}        // write 4 color values
  ldr r4, [r2, #POP2_OFFS]
  ldrb r4, [r4]
  ldr r4, [r3, r4, LSL #2]
  ldr r5, [r2, #POP2_OFFS]
  ldrb r5, [r5]
  ldr r5, [r3, r5, LSL #2]
  ldr r6, [r2, #POP2_OFFS]
  ldrb r6, [r6]
  ldr r6, [r3, r6, LSL #2]
  ldr r7, [r2, #POP2_OFFS]
  ldrb r7, [r7]
  ldr r7, [r3, r7, LSL #2]
  stmia r0!, {r4-r7}        // write next 4 color values

  subs r1, #1
  bne 1b

  pop {r4-r7, pc}

