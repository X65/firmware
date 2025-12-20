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

.section .bss, "aw", %nobits
.align 4
cell_colors:
  .space 4*4  // Reserve 4 words for the multi color array


// Put every function in its own ELF section, to permit linker GC
.macro decl_func name
.section .text.\name, "ax"
.global \name
.type \name,%function
.thumb_func
\name:
.endm


//////////////////// MODE 1 ////////////////////
////           palette bitmap mode          ////

// r0 is output RGB buffer
// r2 is shared_colors array pointer
// r3 is palette pointer
// r5 is bitmap (few columns)
// changes r0,r6-r7
.macro write_pixel_indexed sh_amt mask doubled mapped
  mov r6, r5
  lsrs r6, #\sh_amt
.if \mask == 15
  ands r6, #7
  ands r7, #8 // half-bright bit
.else
  ands r6, #\mask
.endif
.if !\mapped
  beq 1f
.endif
  // load shared color index
  ldrb r6, [r2, r6]
.if \mask == 15
  cbz r7, 2f
  eor r6, r6, #4  // toggle half-bright - XOR 0b0100
2:
.endif
  // load palette entry
  ldr r6, [r3, r6, LSL #2]
.if \doubled
  mov r7, r6
  stmia r0!, {r6-r7}
.else
  stmia r0!, {r6}
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
// r2 is shared_colors array [8]
.macro cgia_encode_mode_1 pixels doubled mapped
  push {r4-r7, lr}

  ldr r3, =cgia_rgb_palette

  ldr r4, =(SIO_BASE + SIO_INTERP0_ACCUM0_OFFSET)

3:
  ldr r5, [r4, #POP0_OFFS]    // load bitmap offset
  // load bitmap data
  ldrb r5, [r5]
.if \pixels > 1
  lsls r5, #8
  ldrb r6, [r4, #POP0_OFFS]
  orrs r5, r6
.endif
.if \pixels > 2
  lsls r5, #8
  ldrb r6, [r4, #POP0_OFFS]
  orrs r5, r6
.endif
.if \pixels > 3
  lsls r5, #8
  ldrb r6, [r4, #POP0_OFFS]
  orrs r5, r6
.endif

.if \pixels == 1
  write_pixel_indexed 7 1 \doubled \mapped
  write_pixel_indexed 6 1 \doubled \mapped
  write_pixel_indexed 5 1 \doubled \mapped
  write_pixel_indexed 4 1 \doubled \mapped
  write_pixel_indexed 3 1 \doubled \mapped
  write_pixel_indexed 2 1 \doubled \mapped
  write_pixel_indexed 1 1 \doubled \mapped
  write_pixel_indexed 0 1 \doubled \mapped
.endif
.if \pixels == 2
  write_pixel_indexed 14 3 \doubled \mapped
  write_pixel_indexed 12 3 \doubled \mapped
  write_pixel_indexed 10 3 \doubled \mapped
  write_pixel_indexed 8 3 \doubled \mapped
  write_pixel_indexed 6 3 \doubled \mapped
  write_pixel_indexed 4 3 \doubled \mapped
  write_pixel_indexed 2 3 \doubled \mapped
  write_pixel_indexed 0 3 \doubled \mapped
.endif
.if \pixels == 3
  write_pixel_indexed 21 7 \doubled \mapped
  write_pixel_indexed 18 7 \doubled \mapped
  write_pixel_indexed 15 7 \doubled \mapped
  write_pixel_indexed 12 7 \doubled \mapped
  write_pixel_indexed 9 7 \doubled \mapped
  write_pixel_indexed 6 7 \doubled \mapped
  write_pixel_indexed 3 7 \doubled \mapped
  write_pixel_indexed 0 7 \doubled \mapped
.endif
.if \pixels == 4
  write_pixel_indexed 28 15 \doubled \mapped
  write_pixel_indexed 24 15 \doubled \mapped
  write_pixel_indexed 20 15 \doubled \mapped
  write_pixel_indexed 16 15 \doubled \mapped
  write_pixel_indexed 12 15 \doubled \mapped
  write_pixel_indexed 8 15 \doubled \mapped
  write_pixel_indexed 4 15 \doubled \mapped
  write_pixel_indexed 0 15 \doubled \mapped
.endif

  subs r1, #1
  bne 3b

  pop {r4-r7, pc}
.endm

decl_func cgia_encode_mode_1_1bpp_doubled_mapped
  cgia_encode_mode_1 1 1 1

decl_func cgia_encode_mode_1_1bpp_doubled_shared
  cgia_encode_mode_1 1 1 0

decl_func cgia_encode_mode_1_1bpp_mapped
  cgia_encode_mode_1 1 0 1

decl_func cgia_encode_mode_1_1bpp_shared
  cgia_encode_mode_1 1 0 0

decl_func cgia_encode_mode_1_2bpp_doubled_mapped
  cgia_encode_mode_1 2 1 1

decl_func cgia_encode_mode_1_2bpp_doubled_shared
  cgia_encode_mode_1 2 1 0

decl_func cgia_encode_mode_1_2bpp_mapped
  cgia_encode_mode_1 2 0 1

decl_func cgia_encode_mode_1_2bpp_shared
  cgia_encode_mode_1 2 0 0

decl_func cgia_encode_mode_1_3bpp_doubled_mapped
  cgia_encode_mode_1 3 1 1

decl_func cgia_encode_mode_1_3bpp_doubled_shared
  cgia_encode_mode_1 3 1 0

decl_func cgia_encode_mode_1_3bpp_mapped
  cgia_encode_mode_1 3 0 1

decl_func cgia_encode_mode_1_3bpp_shared
  cgia_encode_mode_1 3 0 0

decl_func cgia_encode_mode_1_4bpp_doubled_mapped
  cgia_encode_mode_1 4 1 1

decl_func cgia_encode_mode_1_4bpp_doubled_shared
  cgia_encode_mode_1 4 1 0

decl_func cgia_encode_mode_1_4bpp_mapped
  cgia_encode_mode_1 4 0 1

decl_func cgia_encode_mode_1_4bpp_shared
  cgia_encode_mode_1 4 0 0


//////////////////// MODE 2 ////////////////////
////        attribute text/tile mode        ////

// r0 is output RGB buffer
// r1 is scanline data buffer - do not touch!
// r5 is bitmap (8 MSB)
// r6 is foreground color index
// r7 is background color index
// changes r0,r5
.macro write_pixel_bmp doubled mapped
  lsls r5, #1        // shift-out 1 bit
  bcc 1f
    // bit is 1 - draw foreground pixel
  stmia r0!, {r6}
.if \doubled
  stmia r0!, {r6}
.endif
  b 2f
1:  // bit is 0 - draw background pixel
.if \mapped
  stmia r0!, {r7}
.if \doubled
  stmia r0!, {r7}
.endif
.else
.if \doubled
  adds r0, 8  // transparent pixel - skip 2
.else
  adds r0, 4  // transparent pixel - move to next
.endif
.endif
2:
.endm

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

  write_pixel_bmp 0 \mapped
  write_pixel_bmp 0 \mapped
  write_pixel_bmp 0 \mapped
  write_pixel_bmp 0 \mapped
  write_pixel_bmp 0 \mapped
  write_pixel_bmp 0 \mapped
  write_pixel_bmp 0 \mapped
  write_pixel_bmp 0 \mapped

  subs r1, #1
  bne 3b

  pop {r4-r7, pc}
.endm

decl_func cgia_encode_mode_2_shared
	cgia_encode_mode_2_shared_or_mapped 0

decl_func cgia_encode_mode_2_mapped
	cgia_encode_mode_2_shared_or_mapped 1

//////////////////// VT ////////////////////

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

  write_pixel_bmp 0 1
  write_pixel_bmp 0 1
  write_pixel_bmp 0 1
  write_pixel_bmp 0 1
  write_pixel_bmp 0 1
  write_pixel_bmp 0 1
  write_pixel_bmp 0 1
  write_pixel_bmp 0 1

  subs r1, #1
  bne 3b

  pop {r4-r7, pc}


//////////////////// MODE 3 ////////////////////
////         attribute bitmap mode         ////

// r0 is output RGB buffer
// r1 is width in bytes
.macro cgia_encode_mode_3 doubled mapped
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

  write_pixel_bmp \doubled \mapped
  write_pixel_bmp \doubled \mapped
  write_pixel_bmp \doubled \mapped
  write_pixel_bmp \doubled \mapped
  write_pixel_bmp \doubled \mapped
  write_pixel_bmp \doubled \mapped
  write_pixel_bmp \doubled \mapped
  write_pixel_bmp \doubled \mapped

  subs r1, #1
  bne 3b

  pop {r4-r7, pc}
.endm

// r0 is output RGB buffer
// r1 is width in bytes
// r2 is shared_colors array [2]
.macro cgia_encode_mode_3_multi doubled mapped
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

  write_pixel_multi 6 \doubled \mapped
  write_pixel_multi 4 \doubled \mapped
  write_pixel_multi 2 \doubled \mapped
  write_pixel_multi 0 \doubled \mapped

  subs r1, #1
  bne 3b

  pop {r4-r7, pc}
.endm

decl_func cgia_encode_mode_3_doubled_mapped
  cgia_encode_mode_3 1 1

decl_func cgia_encode_mode_3_doubled_shared
  cgia_encode_mode_3 1 0

decl_func cgia_encode_mode_3_mapped
	cgia_encode_mode_3 0 1

decl_func cgia_encode_mode_3_shared
	cgia_encode_mode_3 0 0

decl_func cgia_encode_mode_3_multi_doubled_mapped
  cgia_encode_mode_3_multi 1 1

decl_func cgia_encode_mode_3_multi_doubled_shared
  cgia_encode_mode_3_multi 1 0

decl_func cgia_encode_mode_3_multi_mapped
  cgia_encode_mode_3_multi 0 1

decl_func cgia_encode_mode_3_multi_shared
  cgia_encode_mode_3_multi 0 0


//////////////////// MODE 4 //////////////////// - !!!

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

  write_pixel_multi 6 \doubled \mapped
  write_pixel_multi 4 \doubled \mapped
  write_pixel_multi 2 \doubled \mapped
  write_pixel_multi 0 \doubled \mapped

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


//////////////////// MODE 6 ////////////////////
////       Hold-and-Modify (HAM) mode       ////

// r0 is output RGB buffer
// r2 is base_color array [8]
// r4 is cgia_rgb_palette
// r6 is HAM command
// changes r0,r6,r7,r8,r11,r12
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
  ldr  r6, [r4, r6, LSL #2] // load palette entry - r6 is base color RGB now

  // fast blend using bit shifting and masking voodoo:
  // - remove lowest bits of channels to make place for overflow
  //   and not accumulate overflow with next channel lowest bit
  // - add everything at once (potentially overflowing to next channel)
  // - shift back into position, averaging the sum
  // fast_blend = ((base_color & 0xFEFEFE) + (current_color & 0xFEFEFE)) >> 1;
  ldr r8, =0xFEFEFE
  ands r7, r6, r8   // base color masked
  ands r8, r12, r8  // current color masked
  adds r7, r8
  lsrs r7, #1       // blended value
  // With above operation we lost lowest bits precision
  // We could ignore it, but we will accumulate total error and color fringing over raster time
  // Let's add it back:
  // - extract lowest bits of all channels
  // - add them and average by dividing (shifting)
  // - mask-out highest channel bits that potentially underflowed from next channel
  // - and finally add the fix to the previously blended value forming current_color
  // current_color = fast_blend + ((((base_color & 0x010101) + (current_color & 0x010101)) >> 1) & 0x7F7F7F);
  ldr r8, =0x010101
  ands r6, r6, r8   // base color masked
  ands r12, r12, r8 // current color masked
  adds r6, r12
  lsrs r6, #1
  ldr r8, =0x7F7F7F
  ands r6, r8
  adds r12, r6, r7

  b 111f
102:            // --- Modify a channel
  ands r6, #15  // 00001111 mask data
  // Make changes in quanta
  lsls r6, #4   // shift data to fill byte
  sxtb r6, r6   // sign extend
  lsrs r6, #1   // shift back to total shift of 3 (x8 quanta)

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
  push {r4-r8,r11, lr}

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

  pop {r4-r8,r11, pc}
.endm

decl_func cgia_encode_mode_6
	_cgia_encode_mode_6 0

decl_func cgia_encode_mode_6_doubled
	_cgia_encode_mode_6 1


//////////////////// MODE 7 ////////////////////
////   affine transform chunky pixel mode   ////

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

//////////////////// MODES ////////////////////
decl_func cgia_encode_mode_0_1bpp_doubled_mapped
  bx lr

decl_func cgia_encode_mode_0_1bpp_doubled_shared
  bx lr

decl_func cgia_encode_mode_0_1bpp_mapped
  bx lr

decl_func cgia_encode_mode_0_1bpp_shared
  bx lr

decl_func cgia_encode_mode_0_2bpp_doubled_mapped
  bx lr

decl_func cgia_encode_mode_0_2bpp_doubled_shared
  bx lr

decl_func cgia_encode_mode_0_2bpp_mapped
  bx lr

decl_func cgia_encode_mode_0_2bpp_shared
  bx lr

decl_func cgia_encode_mode_0_3bpp_doubled_mapped
  bx lr

decl_func cgia_encode_mode_0_3bpp_doubled_shared
  bx lr

decl_func cgia_encode_mode_0_3bpp_mapped
  bx lr

decl_func cgia_encode_mode_0_3bpp_shared
  bx lr

decl_func cgia_encode_mode_0_4bpp_doubled_mapped
  bx lr

decl_func cgia_encode_mode_0_4bpp_doubled_shared
  bx lr

decl_func cgia_encode_mode_0_4bpp_mapped
  bx lr

decl_func cgia_encode_mode_0_4bpp_shared
  bx lr

decl_func cgia_encode_mode_0_multi_2bpp_doubled_mapped
  bx lr

decl_func cgia_encode_mode_0_multi_2bpp_doubled_shared
  bx lr

decl_func cgia_encode_mode_0_multi_2bpp_mapped
  bx lr

decl_func cgia_encode_mode_0_multi_2bpp_shared
  bx lr

decl_func cgia_encode_mode_0_multi_3bpp_doubled_mapped
  bx lr

decl_func cgia_encode_mode_0_multi_3bpp_doubled_shared
  bx lr

decl_func cgia_encode_mode_0_multi_3bpp_mapped
  bx lr

decl_func cgia_encode_mode_0_multi_3bpp_shared
  bx lr

decl_func cgia_encode_mode_2_doubled_mapped
  bx lr

decl_func cgia_encode_mode_2_doubled_shared
  bx lr

decl_func cgia_encode_mode_2_multi_doubled_mapped
  bx lr

decl_func cgia_encode_mode_2_multi_doubled_shared
  bx lr

decl_func cgia_encode_mode_2_multi_mapped
  bx lr

decl_func cgia_encode_mode_2_multi_shared
  bx lr
