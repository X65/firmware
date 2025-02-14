# RIA Memory Map

The whole last page of bank 0 (`$00FF00` - `$00FFFF`) is dedicated to internal I/O devices.

"Area $00FE00-$00FEFF is used to handle extension devices. Divided into 8x 32byte chunks.
If you _really_ need to use RAM in this area, use `$00FFF6` register to map RAM chunks in."

Following is the memory mapping of registers of hardware interfaced by RIA.

## RIA - Raspberry Interface Adapter

This document was becoming heavily outdated.
See <https://tinyurl.com/x65-memory-map> for always-current one.

## CGIA - Color Graphic Interface Adaptor

And most of [Atari ANTIC](https://en.wikipedia.org/wiki/ANTIC#Registers) registers will go here…

This document was becoming heavily outdated.
See <https://tinyurl.com/x65-memory-map> for always-current one.

## Yamaha SD-1 (YMF825) - universal Sound Designer 1

`$00FF80` - `$00FFBF`

See [YMF825(SD-1) IF specification](https://github.com/X65/ymf825board/blob/master/manual/fbd_spec1.md#interface-register)
for Interface Register list.
