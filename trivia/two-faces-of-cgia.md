# The Two Faces of CGIA

## Purpose

CGIA has two color personalities:

- A native graphics palette designed to give X65 a recognizable visual character.
- A terminal palette designed to support ANSI/xterm-style text applications.

This document records the design rationale for keeping both palettes available, while treating the native graphics palette as the canonical X65 visual identity.

## Native Graphics Palette

The native CGIA graphics palette is defined as 256 colors arranged as 32 rows of 8 brightness levels. It is generated from a mathematically regular model:

- One neutral grayscale row.
- 31 chromatic hue rows.
- 8 brightness levels per row.
- Colored entries avoid absolute black and white.
- Saturation is lifted at the darkest and brightest levels with a U-shaped curve.

The palette is intended to resemble the color character of machines such as the Atari 8-bit line and Commodore Plus/4. Their GTIA and TED chips produced composite TV color through YUV-like component generation rather than direct RGBI color. That gives their graphics a softer, pastel, balanced quality compared with machines such as the ZX Spectrum or Amstrad CPC, whose RGB/RGBI-derived palettes produce harder full-bright colors.

X65 deliberately takes inspiration from that softer composite-era look, but regularizes it. The clean mathematical structure is part of the X65 distinction: the result should feel related to Atari 8-bit and Plus/4 color, but less historically accidental.

The native palette should remain the default for bitmap, tile, and picture-oriented graphics modes.

Relevant files:

- `src/south/cgia/cgia_palette.h`
- `x65.github.io/media/2024-06-08_colors.html`

## Terminal Palette

CGIA also has a high-resolution terminal-oriented text mode with its own 256-color palette. This palette follows the ANSI/xterm 256-color structure:

- 16 ANSI color roles, split into normal and high-intensity sets.
- A 6 x 6 x 6 RGB color cube.
- A 24-step grayscale ramp.

This palette supports a terminal-like experience for Unix/DOS-style text applications on a 96 x 30 text screen with a 16 x 8 pixel font. Its reds, blues, cyans, magentas, and yellows occupy the color indexes terminal applications expect, so ANSI/xterm text behaves sensibly in this context.

The terminal palette is not trying to express the analog-inspired X65 graphics identity. It expresses the other side of the machine: a clean, high-resolution, workstation-like text environment.

Relevant files:

- `src/south/term/color.c`
- `src/south/term/color.h`

## Artist Feedback

Artists have noted that the native graphics palette lacks almost-full-bright reds and blues useful for strong visual accents. This is a real tradeoff. The native palette avoids hard RGB primaries by design, so it naturally resists some looks:

- Bright warning reds.
- Electric blues.
- High-contrast logos.
- Sharp arcade-style energy effects.
- Some 16-bit console-like color treatments.

Adding a few emergency primaries to the native palette would weaken its internal logic. It would make the palette less coherent while still not providing the flexibility of a true RGB-selected palette.

Exposing the terminal palette to graphics modes is a cleaner escape hatch. It provides bright accent colors without corrupting the native palette.

## Alternate Graphics Palette

Allowing bitmap or low-resolution graphics modes to use the terminal palette creates a second official CGIA graphics face:

- Native graphics palette: pastel, composite-inspired, Atari 8-bit/Plus/4-like, mathematically balanced.
- Terminal graphics palette: ANSI/xterm-derived, brighter, RGB-cube-based, closer in feel to some 16-bit console graphics.

The terminal palette is still restricted. It is not an Amiga-style or VGA-style freely programmable RGB palette. It is a fixed set of 256 colors with its own recognizable structure: 16 ANSI colors, a 216-color cube, and a grayscale ramp. That means it remains a machine-specific constraint rather than becoming arbitrary true-color art.

This may give X65 a broader identity rather than a weaker one. Native-palette graphics express the house style directly. Terminal-palette graphics show a deliberate alternate mode, where the terminal heritage leaks into bitmap art.

## PICO-8 Analogy

PICO-8 is a useful comparison. Its public visual identity is built around a fixed 16-color palette, but its ecosystem also accepts additional hidden colors for games that need a different tone. Those extra colors do not erase the PICO-8 identity because the default palette remains culturally dominant.

X65 can use the same design pattern, with one important difference: the alternate palette should be explicit and named rather than treated as an accident or secret.

The scale of the alternate palette is larger than PICO-8's hidden colors, so the naming matters. If the terminal palette is presented as the normal graphics palette, it can wash out the native X65 look. If it is presented as an alternate ANSI-derived graphics palette, it becomes a sanctioned tool with a clear aesthetic meaning.

## Recommended Framing

Use explicit names:

- `CGIA Native`: the canonical graphics palette.
- `CGIA ANSI` or `CGIA Terminal`: the terminal palette, optionally available to graphics modes.

Recommended policy:

- Default graphics modes to `CGIA Native`.
- Default terminal text mode to `CGIA Terminal`.
- Allow graphics modes to explicitly select `CGIA Terminal`.
- Document terminal-palette graphics as an escape hatch for art direction, not as a replacement for the native graphics identity.

Avoid framing the terminal palette as a fix for missing colors. The native palette is intentionally constrained. The terminal palette is a different constraint with a different heritage.

## Conclusion

X65 should keep the native CGIA palette as its primary visual signature. It gives the machine a distinctive color character: softer than RGBI-era machines, cleaner than historical composite palettes, and recognizable through consistent use.

At the same time, X65 should allow graphics modes to use the terminal palette when artists need brighter accents or a sharper 16-bit-console-like tone. This fits the broader X65 philosophy of choice, while preserving a coherent story:

- X65 native graphics have a pastel, mathematically regular, composite-inspired identity.
- X65 terminal graphics have an ANSI/xterm-derived, high-contrast, workstation-like identity.

Two faces are acceptable as long as they are named, stable, and intentional.

## Addendum: ANSI-16 Divergence (ARNE 16 Mapping)

The Terminal Palette section above describes the lowest 16 entries as "16 standard ANSI colors." In practice those entries diverge from the literal xterm/VGA values. Indices 0-15 are the **ARNE 16** palette (Arne Niklas Jansson) mapped onto the ANSI role slots, rather than the canonical xterm base-16.

### Rationale

The canonical xterm base-16 have a harder VGA/RGBI-derived primary and secondary color character. That is the same harder color family the native-palette discussion associates with RGB/RGBI-era machines and deliberately avoids. Seating those colors unaltered at the foot of the terminal palette put the machine's most frequently used text colors at odds with the rest of its color character.

ARNE 16 is a hand-tuned, perceptually balanced artist palette. Substituting it gives the terminal's base colors a softer, more cohesive tone — closer in spirit to the rest of CGIA — while leaving the ANSI structure intact. The change improves text-mode legibility and color harmony without abandoning the terminal palette's xterm heritage.

### What is preserved

The 16 slots keep their ANSI role semantics: black, red, green, yellow, blue, magenta, cyan, white, and their eight high-intensity variants. Applications that address indices 0-15 by ANSI role still get a color assigned to the expected position. Terminal applications continue to behave; only the exact hues shift.

### Necessary compromise

ARNE 16 contains no pure magenta and no pure cyan. Those role slots are therefore approximated:

- Magenta (5) and bright magenta (13) are stood in for by a brown and an orange.
- Cyan (6) and bright cyan (14) are stood in for by hand-mixed teals.

This is the one place where the mapping knowingly drifts from xterm expectation. The role is honored, but the hue is the closest cohesive match rather than a literal magenta or cyan. Applications that rely on those two indexes reading as strongly saturated magenta/cyan will see a muted substitute.

### Scope

Only indices 0-15 diverge. The 6 x 6 x 6 color cube (16-231) and the 24-step grayscale ramp (232-255) remain the standard xterm definitions, so the bulk of the terminal palette is unchanged and fully xterm-compatible.

Applications that rely on strict XTerm or VGA color appearance are expected to remap their 16-role color usage to hand-picked entries from the cube or grayscale sections instead of directly using the base-16. The available colors in those sections provide enough bright reds, blues, magentas, cyans, and neutral grays to reproduce the expected ANSI or VGA appearance.

One implementation detail is worth recording: index 0 (black) is transparent, while index 16 (an identical RGB black at the start of the cube) is opaque.

Relevant files:

- `src/south/term/color.c`
