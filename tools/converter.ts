#!/usr/bin/env -S deno run --allow-read=. --allow-write=.

import { assert } from "jsr:@std/assert";
import { Args } from "jsr:@std/cli/parse-args";
import { red, yellow } from "jsr:@std/fmt/colors";
import { validateArgs, type ScriptDefinition } from "jsr:@ploiu/arg-helper";
import { getPixels } from "jsr:@unpic/pixels";

const default_args = {
  cell: 8,
  transparent: false,
  palette: "closest",
  format: "multicolor",
  double: false,
  threshold: 33.3,
  greys: true,
};

const supported_palettes = [
  "closest",
  "plus4",
  "plus4-2",
  "fixed([00,]11,22,33)",
];
const supported_formats = ["hires", "multicolor", "chunky"];

// prettier-ignore
const cgia_rgb_palette = [
  0x00000000, 0x00242424, 0x00494949, 0x006d6d6d, 0x00929292, 0x00b6b6b6, 0x00dbdbdb, 0x00ffffff, //
  0x00440505, 0x006f0e0e, 0x008b2626, 0x00a04545, 0x00ba5f5f, 0x00d97474, 0x00f19090, 0x00fabbbb, //
  0x00441105, 0x006f210e, 0x008b3a26, 0x00a05645, 0x00ba705f, 0x00d98874, 0x00f1a390, 0x00fac7bb, //
  0x00441d05, 0x006f330e, 0x008b4d26, 0x00a06845, 0x00ba825f, 0x00d99b74, 0x00f1b690, 0x00fad3bb, //
  0x00442a05, 0x006f460e, 0x008b6126, 0x00a07a45, 0x00ba945f, 0x00d9af74, 0x00f1c890, 0x00fae0bb, //
  0x00443605, 0x006f590e, 0x008b7426, 0x00a08c45, 0x00baa65f, 0x00d9c274, 0x00f1db90, 0x00faecbb, //
  0x00444205, 0x006f6c0e, 0x008b8826, 0x00a09d45, 0x00bab75f, 0x00d9d674, 0x00f1ee90, 0x00faf8bb, //
  0x003a4405, 0x005f6f0e, 0x007b8b26, 0x0091a045, 0x00acba5f, 0x00c9d974, 0x00e2f190, 0x00f0fabb, //
  0x002e4405, 0x004d6f0e, 0x00678b26, 0x0080a045, 0x009aba5f, 0x00b5d974, 0x00cff190, 0x00e4fabb, //
  0x00214405, 0x003a6f0e, 0x00548b26, 0x006ea045, 0x0088ba5f, 0x00a2d974, 0x00bcf190, 0x00d7fabb, //
  0x00154405, 0x00276f0e, 0x00408b26, 0x005ca045, 0x0076ba5f, 0x008ed974, 0x00a9f190, 0x00cbfabb, //
  0x00094405, 0x00146f0e, 0x002d8b26, 0x004ba045, 0x0065ba5f, 0x007bd974, 0x0096f190, 0x00bffabb, //
  0x0005440d, 0x000e6f1a, 0x00268b33, 0x0045a051, 0x005fba6b, 0x0074d981, 0x0090f19c, 0x00bbfac3, //
  0x00054419, 0x000e6f2d, 0x00268b47, 0x0045a062, 0x005fba7c, 0x0074d995, 0x0090f1af, 0x00bbfacf, //
  0x00054425, 0x000e6f40, 0x00268b5a, 0x0045a074, 0x005fba8e, 0x0074d9a8, 0x0090f1c2, 0x00bbfadc, //
  0x00054432, 0x000e6f53, 0x00268b6e, 0x0045a086, 0x005fbaa0, 0x0074d9bc, 0x0090f1d5, 0x00bbfae8, //
  0x0005443e, 0x000e6f66, 0x00268b81, 0x0045a097, 0x005fbab1, 0x0074d9cf, 0x0090f1e8, 0x00bbfaf4, //
  0x00053e44, 0x000e666f, 0x0026818b, 0x004597a0, 0x005fb1ba, 0x0074cfd9, 0x0090e8f1, 0x00bbf4fa, //
  0x00053244, 0x000e536f, 0x00266e8b, 0x004586a0, 0x005fa0ba, 0x0074bcd9, 0x0090d5f1, 0x00bbe8fa, //
  0x00052544, 0x000e406f, 0x00265a8b, 0x004574a0, 0x005f8eba, 0x0074a8d9, 0x0090c2f1, 0x00bbdcfa, //
  0x00051944, 0x000e2d6f, 0x0026478b, 0x004562a0, 0x005f7cba, 0x007495d9, 0x0090aff1, 0x00bbcffa, //
  0x00050d44, 0x000e1a6f, 0x0026338b, 0x004551a0, 0x005f6bba, 0x007481d9, 0x00909cf1, 0x00bbc3fa, //
  0x00090544, 0x00140e6f, 0x002d268b, 0x004b45a0, 0x00655fba, 0x007b74d9, 0x009690f1, 0x00bfbbfa, //
  0x00150544, 0x00270e6f, 0x0040268b, 0x005c45a0, 0x00765fba, 0x008e74d9, 0x00a990f1, 0x00cbbbfa, //
  0x00210544, 0x003a0e6f, 0x0054268b, 0x006e45a0, 0x00885fba, 0x00a274d9, 0x00bc90f1, 0x00d7bbfa, //
  0x002e0544, 0x004d0e6f, 0x0067268b, 0x008045a0, 0x009a5fba, 0x00b574d9, 0x00cf90f1, 0x00e4bbfa, //
  0x003a0544, 0x005f0e6f, 0x007b268b, 0x009145a0, 0x00ac5fba, 0x00c974d9, 0x00e290f1, 0x00f0bbfa, //
  0x00440542, 0x006f0e6c, 0x008b2688, 0x00a0459d, 0x00ba5fb7, 0x00d974d6, 0x00f190ee, 0x00fabbf8, //
  0x00440536, 0x006f0e59, 0x008b2674, 0x00a0458c, 0x00ba5fa6, 0x00d974c2, 0x00f190db, 0x00fabbec, //
  0x0044052a, 0x006f0e46, 0x008b2661, 0x00a0457a, 0x00ba5f94, 0x00d974af, 0x00f190c8, 0x00fabbe0, //
  0x0044051d, 0x006f0e33, 0x008b264d, 0x00a04568, 0x00ba5f82, 0x00d9749b, 0x00f190b6, 0x00fabbd3, //
  0x00440511, 0x006f0e21, 0x008b263a, 0x00a04556, 0x00ba5f70, 0x00d97488, 0x00f190a3, 0x00fabbc7, //
];
Object.freeze(cgia_rgb_palette);

// prettier-ignore
const plus4_palette_to_cgia: Record<number, number> = {
  0x000000:   0, 0xffffff:   7,
  // grey
  0x2c2c2c:   1, 0x3b3b3b:   2, 0x424242:   3, 0x515151:   4, 0x7a7a7a:   5, 0x959595:   6, 0xafafaf:   6, 0xe1e1e1:   7,
  //red
  0x621307:   8, 0x702419:   9, 0x772c21:  10, 0x843b31:  11, 0xac665c:  12, 0xc58178:  13, 0xde9b93:  14, 0xffcfc6:  15,
  //cyan
  0x00424c: 144, 0x00505a: 145, 0x055861: 146, 0x17656f: 147, 0x468e97: 148, 0x62a8b1: 149, 0x7dc2ca: 150, 0xb2f4fc: 151,
  // violet
  0x510378: 192, 0x601685: 193, 0x661e8c: 194, 0x742e99: 195, 0x9c5ac0: 196, 0xb675d9: 197, 0xcf90f2: 198, 0xffc4ff: 199,
  // green
  0x004e00:  88, 0x125d00:  89, 0x1b6400:  90, 0x2b7100:  91, 0x57992e:  92, 0x73b34c:  93, 0x8dcd68:  94, 0xc1fe9d:  95,
  //blue
  0x27188e: 176, 0x36289b: 177, 0x3e30a2: 178, 0x4c3faf: 179, 0x766ad5: 180, 0x9185ed: 181, 0xab9fff: 182, 0xddd2ff: 183,
  //yellow
  0x303e00:  48, 0x3f4c00:  49, 0x475400:  50, 0x556200:  51, 0x7e8a13:  52, 0x99a433:  53, 0xb3be51:  54, 0xe5f088:  55,
  //orange
  0x582100:  24, 0x663100:  25, 0x6d3900:  26, 0x7a4709:  27, 0xa2713a:  28, 0xbb8c57:  29, 0xd5a673:  30, 0xffd9a8:  31,
  // brown
  0x463000:  40, 0x553f00:  41, 0x5c4700:  42, 0x6a5500:  43, 0x927e20:  44, 0xac993e:  45, 0xc6b35b:  46, 0xf7e591:  47,
  // dark green
  0x244400:  64, 0x345200:  65, 0x3b5900:  66, 0x4a6700:  67, 0x748f14:  68, 0x8faa34:  69, 0xa9c351:  70, 0xdbf588:  71,
  // pink
  0x630448: 224, 0x711656: 225, 0x771f5d: 226, 0x852f6b: 227, 0xac5a93: 228, 0xc676ad: 229, 0xdf91c7: 230, 0xffc4f9: 231,
  // turqouise
  0x004e0c:  96, 0x005c1d:  97, 0x046325:  98, 0x177135:  99, 0x459960: 100, 0x62b37b: 101, 0x7dcc96: 102, 0xb1fec9: 103,
  // sky
  0x0e2784: 160, 0x1f3691: 161, 0x273e98: 162, 0x364ca5: 163, 0x6276cb: 164, 0x7d91e4: 165, 0x97abfd: 166, 0xcbddff: 167,
  // dark violet
  0x33118e: 184, 0x42229b: 185, 0x492aa1: 186, 0x5739ae: 187, 0x8064d4: 188, 0x9b80ed: 189, 0xb59aff: 190, 0xe7cdff: 191,
  // grass
  0x184800:  72, 0x285700:  73, 0x305e00:  74, 0x3f6b00:  75, 0x6a9419:  76, 0x85ae38:  77, 0x9fc755:  78, 0xd2f98c:  79,
};
Object.freeze(plus4_palette_to_cgia);
// prettier-ignore
const plus4_palette2_to_cgia: Record<number, number> = {
  0x000000:   0,
  0x202020:   1, 0x404040:   2, 0x606060:   3, 0x808080:   4, 0x9f9f9f:   5, 0xbfbfbf:   6, 0xdfdfdf:   7, 0xffffff:   7,
  0x5d0800:  16, 0x7d2819:  17, 0x9c4839:  18, 0xbc6859:  19, 0xdc8879:  20, 0xfca899:  21, 0xffc8b9:  22, 0xffe8d9:  23,
  0x003746: 144, 0x035766: 145, 0x237786: 146, 0x4397a6: 147, 0x63b7c6: 148, 0x82d7e6: 149, 0xa2f7ff: 150, 0xc2ffff: 151,
  0x5d006d: 216, 0x7d128d: 217, 0x9c32ac: 218, 0xbc52cc: 219, 0xdc71ec: 220, 0xfc91ff: 221, 0xffb1ff: 222, 0xffd1ff: 223,
  0x004e00:  88, 0x036e00:  89, 0x238e13:  90, 0x43ad33:  91, 0x63cd53:  92, 0x82ed72:  93, 0xa2ff92:  94, 0xc2ffb2:  95,
  0x20116d: 184, 0x40318d: 185, 0x6051ac: 186, 0x8071cc: 187, 0x9f90ec: 188, 0xbfb0ff: 189, 0xdfd0ff: 190, 0xfff0ff: 191,
  0x202f00:  56, 0x404f00:  57, 0x606f13:  58, 0x808e33:  59, 0x9fae53:  60, 0xbfce72:  61, 0xdfee92:  62, 0xffffb2:  63,
  0x004600:  96, 0x036619:  97, 0x238639:  98, 0x43a659:  99, 0x63c679: 100, 0x82e699: 101, 0xa2ffb9: 102, 0xc2ffd9: 103,
  0x5d1000:  24, 0x7d3000:  25, 0x9c5013:  26, 0xbc6f33:  27, 0xdc8f53:  28, 0xfcaf72:  29, 0xffcf92:  30, 0xffefb2:  31,
  0x3e1f00:  40, 0x5e3f00:  41, 0x7e5f13:  42, 0x9e7f33:  43, 0xbe9f53:  44, 0xdebf72:  45, 0xfedf92:  46, 0xfffeb2:  47,
  0x013e00:  72, 0x215e00:  73, 0x417e13:  74, 0x619e33:  75, 0x81be53:  76, 0xa1de72:  77, 0xc1fe92:  78, 0xe1ffb2:  79,
  0x5d0120: 240, 0x7d2140: 241, 0x9c4160: 242, 0xbc6180: 243, 0xdc809f: 244, 0xfca0bf: 245, 0xffc0df: 246, 0xffe0ff: 247,
  0x003f20: 120, 0x035f40: 121, 0x237f60: 122, 0x439e80: 123, 0x63be9f: 124, 0x82debf: 125, 0xa2fedf: 126,
  0x00306d: 152, 0x03508d: 153, 0x2370ac: 154, 0x4390cc: 155, 0x63afec: 156, 0x82cfff: 157, 0xa2efff: 158,
  0x3e016d: 208, 0x5e218d: 209, 0x7e41ac: 210, 0x9e61cc: 211, 0xbe81ec: 212, 0xdea1ff: 213, 0xfec1ff: 214, 0xffe1ff: 215,
};
Object.freeze(plus4_palette2_to_cgia);

type Color = [number, number, number]; // [R, G, B]

let args = {} as Args;

let fixed_palette: number[] = [];

function fromBGR(pixel: number): Color {
  assert(pixel >= 0);
  return [pixel & 0xff, (pixel >> 8) & 0xff, (pixel >> 16) & 0xff];
}
function fromRGB(pixel: number): Color {
  assert(pixel >= 0);
  return [(pixel >> 16) & 0xff, (pixel >> 8) & 0xff, pixel & 0xff];
}

// https://www.compuphase.com/cmetric.htm
function colorDistance(C1: Color, C2: Color) {
  const [R1, G1, B1] = C1;
  const [R2, G2, B2] = C2;

  const Rmean = (R1 + R2) / 2;
  const dR2 = Math.pow(R1 - R2, 2);
  const dG2 = Math.pow(G1 - G2, 2);
  const dB2 = Math.pow(B1 - B2, 2);
  return Math.sqrt(
    ((512 + Rmean) * dR2) / 256 + 4 * dG2 + ((767 - Rmean) * dB2) / 256
  );
  // return Math.sqrt(
  //   Math.pow(C1[0] - C2[0], 2) +
  //     Math.pow(C1[1] - C2[1], 2) +
  //     Math.pow(C1[2] - C2[2], 2)
  // );
}

// https://stackoverflow.com/a/56678483/139456
function sRGBtoLin(colorChannel: number) {
  // Send this function a decimal sRGB gamma encoded color value
  // between 0.0 and 1.0, and it returns a linearized value.
  if (colorChannel <= 0.04045) {
    return colorChannel / 12.92;
  } else {
    return Math.pow((colorChannel + 0.055) / 1.055, 2.4);
  }
}

function sRGBtoY(cl: Color) {
  // Convert all sRGB 8 bit integer values to decimal 0.0-1.0
  const vR = cl[0] / 255;
  const vG = cl[1] / 255;
  const vB = cl[2] / 255;
  // To find Luminance (Y) apply the standard coefficients for sRGB
  return (
    0.2126 * sRGBtoLin(vR) + 0.7152 * sRGBtoLin(vG) + 0.0722 * sRGBtoLin(vB)
  );
}

function YtoLstar(Y: number) {
  // Send this function a luminance value between 0.0 and 1.0,
  // and it returns L* which is "perceptual lightness"
  if (Y <= 216 / 24389) {
    // The CIE standard states 0.008856 but 216/24389 is the intent for 0.008856451679036
    return Y * (24389 / 27); // The CIE standard states 903.3, but 24389/27 is the intent, making 903.296296296296296
  } else {
    return Math.pow(Y, 1 / 3) * 116 - 16;
  }
}

function sRGBtoLstar(cl: Color) {
  return YtoLstar(sRGBtoY(cl));
}

function lightnessDistance(C1: Color, C2: Color) {
  return Math.abs(sRGBtoLstar(C1) - sRGBtoLstar(C2));
}

function toHEX(n: number) {
  assert(n < 256);
  assert(n >= 0);
  return `0${n.toString(16)}`.slice(-2);
}

function toCSS([R, G, B]: Color) {
  return `#${toHEX(R)}${toHEX(G)}${toHEX(B)}`;
}

function closestColor(rgb: Color, alg = args.palette): number {
  let idx = Infinity;
  let distance = Infinity;
  switch (alg) {
    case "closest":
      {
        for (let i = 0; i < cgia_rgb_palette.length; ++i) {
          const [R, G, B] = fromRGB(cgia_rgb_palette[i]);
          const d = colorDistance([R, G, B], rgb);
          if (d < distance) {
            idx = i;
            distance = d;
          }
        }
      }
      break;
    case "fixed":
      {
        for (let i = 0; i < fixed_palette.length; ++i) {
          const color = fixed_palette[i];
          const [R, G, B] = fromRGB(cgia_rgb_palette[color]);
          const d = colorDistance([R, G, B], rgb);
          if (d < distance) {
            idx = color;
            distance = d;
          }
        }
      }
      break;
    case "plus4":
    case "plus4-2":
      {
        const p4p = Object.entries(
          alg === "plus4" ? plus4_palette_to_cgia : plus4_palette2_to_cgia
        );
        for (let i = 0; i < p4p.length; ++i) {
          const [R, G, B] = fromRGB(Number(p4p[i][0]));
          const d = colorDistance([R, G, B], rgb);
          if (d < distance) {
            idx = p4p[i][1];
            distance = d;
          }
        }
      }
      break;
    default: {
      abort(`Unsupported palette: ${alg}`);
    }
  }
  assert(idx < 256);
  return idx;
}

const palette_map = Array.from({ length: 256 }, () => [] as Color[]);

function colorIn(list: Color[], cl: Color) {
  return list.some((c) => c.toString() === cl.toString());
}

function addColorToMap(map: Record<number, Color[]>, idx: number, cl: Color) {
  if (!colorIn(map[idx], cl)) {
    map[idx].push(cl);
    return true;
  }
  return false;
}
function addColor(cl: Color) {
  const idx = closestColor(cl);
  addColorToMap(palette_map, idx, cl);
}
function getColorIdx(cl: Color) {
  const idx = Object.values(palette_map).findIndex((p) =>
    p.some((c) => c.toString() === cl.toString())
  );
  assert(idx >= 0);
  return idx;
}

function printClashes() {
  let clashes = 0;
  for (let idx = 0; idx < palette_map.length; ++idx) {
    if (palette_map[idx].length > 1) {
      clashes += 1;
      console.warn(
        yellow(`Color ${idx} clashes: ${palette_map[idx].map(toCSS)}`)
      );
    }
  }
  return clashes;
}
function printPalette() {
  for (let idx = 0; idx < palette_map.length; ++idx) {
    if (palette_map[idx].length > 0) {
      console.log(idx, palette_map[idx]);
    }
  }
}

let out = Deno.stdout.writable.getWriter();
function print(s: string) {
  return out.write(new TextEncoder().encode(s));
}

const argDef: ScriptDefinition = {
  arguments: [
    {
      name: "format",
      shortName: "f",
      description: `Output format [${supported_formats}] (Default: ${default_args.format})`,
      required: false,
    },
    {
      name: "double",
      shortName: "d",
      description: `Horizontal pixel doubling (Default: ${default_args.double})`,
      required: false,
    },
    {
      name: "cell",
      shortName: "c",
      description: `Row cell height (Default: ${default_args.cell})`,
      required: false,
    },
    {
      name: "palette",
      shortName: "p",
      description: `Palette transformation [${supported_palettes}] (Default: ${default_args.palette})`,
      required: false,
    },
    {
      name: "transparent",
      shortName: "t",
      description: `Should handle transparency (Default: ${default_args.transparent})`,
      required: false,
    },
    {
      name: "greys",
      shortName: "g",
      description: `Should keep greys grey (Default: ${default_args.greys})`,
      required: false,
    },
    {
      name: "threshold",
      shortName: "b",
      description: `Brightness threshold for color mapping (Default: ${default_args.threshold})`,
      required: false,
    },
    {
      name: "out",
      shortName: "o",
      description: `Output .h-eader file (Default: stdout)`,
      required: false,
    },
  ],
  scriptDescription: "Converts .png files into X65 data in .h-eader format.",
  helpFlags: ["help", "?"],
};

function abort(msg: string): never {
  console.error(red(msg));
  Deno.exit(1);
}

// --- GENERATORS ---
// multicolor colors are:
// 00 - shared color 0
// 01 - attribute foreground color (1)
// 10 - attribute background color (0)
// 11 - shared color 1
function genMultiColorLine(
  cells: number[][],
  shared_colors: [number | undefined, number | undefined]
): [number, number[][], number[][]] {
  const cells_colors = Array.from(cells, () => [] as number[]);
  const cells_pixels = Array.from(cells, () => [] as number[]);
  let cumulative_error = 0;
  for (let c = 0; c < cells.length; ++c) {
    const cell = cells[c];
    const cell_colors = cells_colors[c];
    const cell_pixels = cells_pixels[c];
    if (args.palette === "fixed") {
      shared_colors[0] = fixed_palette[0];
      shared_colors[1] = fixed_palette[3];
      cell_colors[0] = fixed_palette[1];
      cell_colors[1] = fixed_palette[2];
    }
    for (let i = 0; i < cell.length; i += args.double ? 2 : 1) {
      const color = cell[i];
      let pixel;
      if (args.transparent ? color < 0 : color === shared_colors[0]) {
        pixel = 0; // 00
      } else if (color === shared_colors[1]) {
        pixel = 3; // 11
      } else {
        const cell_color = cell_colors.indexOf(color);
        if (cell_color >= 0) {
          pixel = cell_color + 1;
          assert(pixel < 3);
        } else {
          if (cell_colors.length >= 2) {
            // we have to much colors in cell
            // find the closest color to fit
            const cl = fromRGB(cgia_rgb_palette[color]);
            const colors = (
              [
                [shared_colors[0], 0],
                [shared_colors[1], 3],
                [cell_colors[0], 1],
                [cell_colors[1], 2],
              ].filter(([idx]) => idx! >= 0) as [number, number][]
            ).map(([idx, px]) => [
              idx,
              colorDistance(cl, fromRGB(cgia_rgb_palette[idx!])),
              px,
            ]);
            colors.sort(([idx1, d1], [idx2, d2]) =>
              d1 === d2 ? idx1 - idx2 : d1 - d2
            );
            cumulative_error += colors[0][1];
            pixel = colors[0][2];
          } else {
            cell_colors.push(color);
            pixel = cell_colors.length;
          }
        }
      }
      cell_pixels.push(pixel);
    }
  }
  return [cumulative_error, cells_colors, cells_pixels];
}
function genHiresLine(cells: number[][]): [number, number[][], number[][]] {
  const cells_colors = Array.from(cells, () => [] as number[]);
  const cells_pixels = Array.from(cells, () => [] as number[]);
  let cumulative_error = 0;
  for (let c = 0; c < cells.length; ++c) {
    const cell = cells[c];
    const cell_colors = cells_colors[c];
    const cell_pixels = cells_pixels[c];
    for (let i = 0; i < cell.length; i += args.double ? 2 : 1) {
      const color = cell[i];
      let pixel;
      const cell_color = cell_colors.indexOf(color);
      if (cell_color >= 0) {
        pixel = cell_color;
      } else {
        if (cell_colors.length >= 2) {
          // we have to much colors in cell
          // find the closest color to fit
          const cl = fromRGB(cgia_rgb_palette[color]);
          const colors = (
            [
              [cell_colors[0], 0],
              [cell_colors[1], 1],
            ].filter(([idx]) => idx! >= 0) as [number, number][]
          ).map(([idx, px]) => [
            idx,
            colorDistance(cl, fromRGB(cgia_rgb_palette[idx!])),
            px,
          ]);
          colors.sort(([idx1, d1], [idx2, d2]) =>
            d1 === d2 ? idx1 - idx2 : d1 - d2
          );
          cumulative_error += colors[0][1];
          pixel = colors[0][2];
        } else {
          pixel = cell_colors.length;
          cell_colors.push(color);
        }
      }
      assert(pixel < 2);
      cell_pixels.push(pixel);
    }
  }
  return [cumulative_error, cells_colors, cells_pixels];
}

// --- MAIN ---
if (import.meta.main) {
  args = {
    ...default_args,
    ...Object.fromEntries(
      Object.entries(
        validateArgs({
          args: Deno.args,
          definition: argDef,
          parseOptions: {
            string: ["format", "width", "height", "palette", "cell"],
            negatable: ["transparent", "greys"],
          },
        })
      ).filter(([_, value]) => value !== undefined)
    ),
  } as unknown as Args;

  const file_name = args._.shift();

  if (!file_name || args._.length > 0) {
    console.error(red("Requires one input file."));
    Deno.exit(1);
  }

  if (args.palette.includes(",")) {
    fixed_palette = args.palette.split(",").map(Number);
    args.palette = "fixed";
    if (args.transparent) fixed_palette.unshift(0xff);
  }

  // console.debug(args);

  let file;
  try {
    file = await Deno.readFile(String(file_name));
  } catch (err) {
    abort(String(err));
  }

  const { width, height, data } = await getPixels(file);
  const BYTES_PER_SAMPLE = 4;

  const getPixelN = (idx: number): number => {
    assert(Math.round(idx / BYTES_PER_SAMPLE) == idx / BYTES_PER_SAMPLE);
    const A = data[idx + 3];
    if (A !== 0 && A !== 255) {
      abort(`Unsupported alpha value ${A} @${idx / BYTES_PER_SAMPLE}`);
    }
    const R = data[idx + 0];
    const G = data[idx + 1];
    const B = data[idx + 2];

    return A ? R + G * 256 + B * 256 * 256 : -1;
  };
  const getPixelXY = (x: number, y: number): number => {
    return getPixelN((width * y + x) * BYTES_PER_SAMPLE);
  };

  let column_width: number =
    args.format === "hires" ? (args.double ? 16 : 8) : args.double ? 8 : 4;
  let cell_height: number = Number(args.cell);

  if (args.format === "chunky") {
    column_width = 1;
    cell_height = 1;
  }

  const columns = width / column_width;
  const rows = height / cell_height;

  if (columns != Math.floor(columns)) {
    abort(
      red(
        `${file_name} width (${width}px) should be divisible by: ${column_width}`
      )
    );
  }

  if (rows != Math.floor(rows)) {
    abort(
      red(
        `${file_name} height (${height}px) should be divisible by: ${cell_height}`
      )
    );
  }

  // statistics gathering
  for (let i = 0; i < data.length; i += BYTES_PER_SAMPLE) {
    const color = getPixelN(i);
    if (color >= 0) {
      addColor(fromBGR(color));
    }
  }

  // optimizing
  if (printClashes()) {
    console.log("Optimizing palette...");

    // used to track where color has already been
    const occupancies = Array.from({ length: 256 }, () => [] as Color[]);

    let changed;
    do {
      // repeat until a run does nothing
      changed = false;

      for (let idx = 0; idx < palette_map.length; ++idx) {
        if (palette_map[idx].length > 1) {
          let distances: [Color, number, number][] = [];
          if (idx < 8) {
            // if grey
            if (args.greys) {
              // work only within current color band
              const il = Math.max(Math.floor(idx / 8) * 8, idx - 1);
              const ih = Math.min((Math.floor(idx / 8) + 1) * 8 - 1, idx + 1);
              // compute distances to the lower color
              if (il !== idx)
                for (const cl of palette_map[idx]) {
                  distances.push([
                    cl,
                    colorDistance(cl, fromRGB(cgia_rgb_palette[il])),
                    il,
                  ]);
                }
              // compute distances to higher color
              if (ih !== idx)
                for (const cl of palette_map[idx]) {
                  distances.push([
                    cl,
                    colorDistance(cl, fromRGB(cgia_rgb_palette[ih])),
                    ih,
                  ]);
                }
            } else {
              // find closest brightness color
              for (const cl of palette_map[idx]) {
                Object.entries(cgia_rgb_palette).map(([no, rgb]) => {
                  distances.push([
                    cl,
                    lightnessDistance(cl, fromRGB(rgb)),
                    Number(no),
                  ]);
                });
              }
            }
          } else {
            for (const cl of palette_map[idx]) {
              Object.entries(cgia_rgb_palette).map(([no, rgb]) => {
                distances.push([
                  cl,
                  colorDistance(cl, fromRGB(rgb)),
                  Number(no),
                ]);
              });
            }
          }
          let max_d_l = Infinity;
          let max_d_h = Infinity;
          distances = distances
            .filter(([f_cl, f_d, f_idx]) => {
              // skip colors that would change its brightness too much
              // TODO: make it a CLI argument
              const l_src = sRGBtoLstar(f_cl);
              const l_dst = sRGBtoLstar(fromRGB(cgia_rgb_palette[f_idx]));
              const l_delta = Math.abs(l_src - l_dst);
              if (l_delta < args.threshold) {
                return true;
              }
              if (f_idx < idx && f_d < max_d_l) max_d_l = f_d;
              if (f_idx > idx && f_d < max_d_h) max_d_h = f_d;
              return false;
            })
            .filter(([f_cl, f_d, f_idx]) => {
              // skip colors that already was in place they would move
              if (colorIn(occupancies[f_idx], f_cl)) {
                if (f_idx < idx && f_d < max_d_l) max_d_l = f_d;
                if (f_idx > idx && f_d < max_d_h) max_d_h = f_d;
                return false;
              }
              return true;
            });
          // skip colors that have worse match than colors skipped in above filter
          distances = distances
            .filter(([_f_cl, f_d, f_idx]) => {
              return (
                (f_idx < idx && f_d < max_d_l) || (f_idx > idx && f_d < max_d_h)
              );
            })
            .sort(([_c1, d1, i1], [_c2, d2, i2]) =>
              d1 === d2 ? i1 - i2 : d1 - d2
            );
          if (distances.length > 0) {
            const [moved_cl, _moved_d, moved_idx] = distances.shift()!;
            palette_map[idx] = palette_map[idx].filter(
              (cl) => cl.toString() !== moved_cl.toString()
            );
            palette_map[moved_idx].push(moved_cl);
            addColorToMap(occupancies, idx, moved_cl);
            changed = true;
          }
        }
      }
    } while (changed);
  }
  printClashes();

  // conversion
  const cells = Array.from({ length: rows * columns }, () => [] as number[]);

  // first, distribute pixels among cells
  for (let y = 0; y < height; ++y) {
    for (let x = 0; x < width; ++x) {
      const pixel = getPixelXY(x, y);
      let color_no: number;
      if (pixel < 0) {
        if (!args.transparent)
          abort(`Transparency (@${x},${y}) allowed only in --transparent mode`);
        color_no = -1;
      } else {
        const pixel_color = fromBGR(pixel);
        color_no = getColorIdx(pixel_color);
      }
      const cell_idx =
        Math.floor(x / column_width) + Math.floor(y / cell_height) * columns;
      cells[cell_idx].push(color_no);
    }
  }

  // find out most common colors in rows
  const row_colors_histogram = Array.from(
    { length: rows },
    () => [] as [number, number][]
  );
  for (let i = 0; i < cells.length; ++i) {
    const cell = cells[i];
    const row = Math.floor(i / columns);
    const colors = row_colors_histogram[row];
    for (const cl of cell) {
      if (cl < 0) continue;
      const color_counter = colors.find(([c, _n]) => c === cl);
      if (color_counter) color_counter[1] += 1;
      else colors.push([cl, 1]);
    }
  }
  // sort on occurrences, most occurring first
  for (const row of row_colors_histogram)
    row.sort(([c1, n1], [c2, n2]) => (n1 === n2 ? c1 - c2 : n1 - n2)).reverse();
  const row_colors = row_colors_histogram.map((row) => row.map(([cl]) => cl));

  // next iterate cells and generate picture data
  console.log("Mangling picture data...");
  const cell_colors: number[][] = [];
  const cell_pixels: number[][] = [];
  const shared_colors: number[][] = [];

  for (let c = 0; c < cells.length; c += columns) {
    const cells_row = cells.slice(c, c + columns);
    switch (args.format) {
      case "multicolor":
        {
          // Let's try with most common colors first
          const row_no = Math.floor(c / columns);
          let shcl1: number | undefined;
          let shcl2: number | undefined;
          if (args.transparent) {
            shcl2 = row_colors[row_no][0];
          } else {
            shcl1 = row_colors[row_no][0];
            shcl2 = row_colors[row_no][1];
          }
          let best_row: [
            number,
            number[][],
            number[][],
            number | undefined,
            number | undefined
          ] = [...genMultiColorLine(cells_row, [shcl1, shcl2]), shcl1, shcl2];
          if (best_row[0] !== 0) {
            // If not match, let's try all other options
            outer: for (let idx1 = 0; idx1 < row_colors.length; ++idx1) {
              for (let idx2 = 0; idx2 < row_colors.length; ++idx2) {
                if (args.transparent) {
                  shcl2 = row_colors[idx1][0];
                } else {
                  shcl1 = row_colors[idx1][0];
                  shcl2 = row_colors[idx2][1];
                }
                if (shcl1 === shcl2) continue;
                const result = genMultiColorLine(cells_row, [shcl1, shcl2]);
                if (result[0] < best_row[0]) {
                  best_row = [...result, shcl1, shcl2];
                }
                if (best_row[0] === 0) break outer;
              }
            }
          }
          if (best_row[0] !== 0) {
            console.warn(
              yellow(
                `Row ${row_no} fuzzy matched (Δ${Math.round(best_row[0])})`
              )
            );
          }
          cell_colors.push(...best_row[1]);
          cell_pixels.push(...best_row[2]);
          assert(
            (best_row[3] === undefined && best_row[4] === undefined) ||
              best_row[3] !== best_row[4]
          );
          assert(!(args.transparent && best_row[3] !== undefined));
          shared_colors.push([best_row[3] ?? 0xff, best_row[4] ?? 0xff]);
        }
        break;
      case "hires":
        {
          const row: [number, number[][], number[][]] = genHiresLine(cells_row);
          cell_colors.push(...row[1]);
          cell_pixels.push(...row[2]);
        }
        break;
      case "chunky":
        // No need to mangle pixels
        break;
      default:
        abort(`Unknown format: ${args.format}`);
    }
  }

  // now generate header data
  console.log("Writing picture data...");
  if (args.out) {
    const file = await Deno.create(args.out);
    out = file.writable.getWriter();
  }
  switch (args.format) {
    case "multicolor":
      {
        print(
          `static uint8_t __attribute__((aligned(4))) bitmap_data[${
            cell_pixels.length * args.cell
          }] = {\n`
        );
        for (let i = 0; i < cell_pixels.length; ++i) {
          const cell_pixels_cell = cell_pixels[i];
          for (let p = 0; p < cell_pixels_cell.length; p += 4) {
            const data =
              (cell_pixels_cell[p + 0] << 6) +
              (cell_pixels_cell[p + 1] << 4) +
              (cell_pixels_cell[p + 2] << 2) +
              cell_pixels_cell[p + 3];
            print(`0x${toHEX(data)}, `);
          }
          if ((i + 1) % columns === 0) print(`// ${Math.floor(i / columns)}\n`);
        }
        print(`};\n\n`);

        print(
          `static uint8_t __attribute__((aligned(4))) color_data[${cell_colors.length}] = {\n`
        );
        for (let i = 0; i < cell_colors.length; ++i) {
          print(`0x${toHEX(cell_colors[i][1] || 0)}, `);
          if ((i + 1) % columns === 0) print(`// ${Math.floor(i / columns)}\n`);
        }
        print(`};\n\n`);

        print(
          `static uint8_t __attribute__((aligned(4))) bkgnd_data[${cell_colors.length}] = {\n`
        );
        for (let i = 0; i < cell_colors.length; ++i) {
          print(`0x${toHEX(cell_colors[i][0] || 0)}, `);
          if ((i + 1) % columns === 0) print(`// ${Math.floor(i / columns)}\n`);
        }
        print(`};\n\n`);

        print(`static const uint16_t video_offset = 0x0000;\n`);
        print(`static const uint16_t color_offset = 0x5000;\n`);
        print(`static const uint16_t bkgnd_offset = 0xA000;\n`);
        print(`static const uint16_t dl_offset = 0xF000;\n`);
        print(
          `static uint8_t __attribute__((aligned(4))) display_list[] = {\n`
        );
        print(
          `0x73, (video_offset & 0xFF), ((video_offset >> 8) & 0xFF),  // LMS\n`
        );
        print(
          `(color_offset & 0xFF), ((color_offset >> 8) & 0xFF),        // LFS\n`
        );
        print(
          `(bkgnd_offset & 0xFF), ((bkgnd_offset >> 8) & 0xFF),        // LBS\n`
        );
        let sh_0 = -1;
        let sh_1 = -1;
        for (let i = 0; i < shared_colors.length; ++i) {
          if (sh_0 !== (shared_colors[i][0] || 0)) {
            sh_0 = shared_colors[i][0] || 0;
            print(`0x44, 0x${toHEX(sh_0).toUpperCase()}, `);
          }
          if (sh_1 !== (shared_colors[i][1] || 0)) {
            sh_1 = shared_colors[i][1] || 0;
            print(`0x54, 0x${toHEX(sh_1).toUpperCase()}, `);
          }
          print(`0x0D, // MODE5\n`);
        }
        print(
          `0x82, (dl_offset & 0xFF), ((dl_offset >> 8) & 0xFF)  // JMP to begin of DL and wait for Vertical BLank\n`
        );
        print(`};\n\n`);
        print(
          `static const uint8_t border_columns = ${
            (384 - columns * column_width) / 16
          };\n`
        );
      }
      break;
    case "hires":
      {
        print(
          `static uint8_t __attribute__((aligned(4))) bitmap_data[${
            cell_pixels.length * args.cell
          }] = {\n`
        );
        for (let i = 0; i < cell_pixels.length; ++i) {
          const cell_pixels_cell = cell_pixels[i];
          for (let p = 0; p < cell_pixels_cell.length; p += 8) {
            const data =
              (cell_pixels_cell[p + 0] << 7) +
              (cell_pixels_cell[p + 1] << 6) +
              (cell_pixels_cell[p + 2] << 5) +
              (cell_pixels_cell[p + 3] << 4) +
              (cell_pixels_cell[p + 4] << 3) +
              (cell_pixels_cell[p + 5] << 2) +
              (cell_pixels_cell[p + 6] << 1) +
              cell_pixels_cell[p + 7];
            print(`0x${toHEX(data)}, `);
          }
          if ((i + 1) % columns === 0) print(`// ${Math.floor(i / columns)}\n`);
        }
        print(`};\n\n`);

        print(
          `static uint8_t __attribute__((aligned(4))) color_data[${cell_colors.length}] = {\n`
        );
        for (let i = 0; i < cell_colors.length; ++i) {
          print(`0x${toHEX(cell_colors[i][1] || 0)}, `);
          if ((i + 1) % columns === 0) print(`// ${Math.floor(i / columns)}\n`);
        }
        print(`};\n\n`);

        print(
          `static uint8_t __attribute__((aligned(4))) bkgnd_data[${cell_colors.length}] = {\n`
        );
        for (let i = 0; i < cell_colors.length; ++i) {
          print(`0x${toHEX(cell_colors[i][0] || 0)}, `);
          if ((i + 1) % columns === 0) print(`// ${Math.floor(i / columns)}\n`);
        }
        print(`};\n\n`);

        print(`static const uint16_t video_offset = 0x0000;\n`);
        print(`static const uint16_t color_offset = 0x5000;\n`);
        print(`static const uint16_t bkgnd_offset = 0xA000;\n`);
        print(`static const uint16_t dl_offset = 0xF000;\n`);
        print(
          `static uint8_t __attribute__((aligned(4))) display_list[] = {\n`
        );
        print(
          `0x73, (video_offset & 0xFF), ((video_offset >> 8) & 0xFF),  // LMS\n`
        );
        print(
          `(color_offset & 0xFF), ((color_offset >> 8) & 0xFF),        // LFS\n`
        );
        print(
          `(bkgnd_offset & 0xFF), ((bkgnd_offset >> 8) & 0xFF),        // LBS\n`
        );
        for (let i = 0; i < rows; ++i) {
          print(`0x0B, // MODE3\n`);
        }
        print(
          `0x82, (dl_offset & 0xFF), ((dl_offset >> 8) & 0xFF)  // JMP to begin of DL and wait for Vertical BLank\n`
        );
        print(`};\n\n`);
        print(
          `static const uint8_t border_columns = ${
            (384 - columns * column_width) / 16
          };\n`
        );
      }
      break;
    case "chunky":
      {
        print(
          `static uint8_t __attribute__((aligned(4))) pixel_data[${
            data.length / BYTES_PER_SAMPLE
          }] = {\n`
        );
        for (let i = 0; i < data.length; i += BYTES_PER_SAMPLE) {
          const pixel = getPixelN(i);
          const pixel_color = fromBGR(pixel);
          const color_no = getColorIdx(pixel_color);
          print(`0x${toHEX(color_no)}, `);
          if ((i / BYTES_PER_SAMPLE + 1) % width === 0)
            print(`// ${Math.floor(i / BYTES_PER_SAMPLE / width)}\n`);
        }
        print(`};\n\n`);
        print(`static const uint8_t pixel_width = ${width};\n`);
        print(`static const uint8_t pixel_height = ${height};\n`);
      }
      break;
    default:
      abort(`Unknown format: ${args.format}`);
  }

  if (args.out) {
    out.close();
  }
}
