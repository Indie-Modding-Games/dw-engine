# TIM, stacked TIMs and fixed-stride packs

Reference for the original's images as `formats/image` (`dwe::formats::image`) reads them. The overall plan
is in [original formats](../architecture/formats.md). Citations are to the decomp (`dw_decomp`, `src/`); there is
no disc data here.

All integers are little-endian. The readers receive a `std::span<const std::byte>`, check every offset and
return an error if the file is malformed; they never read outside the buffer.

## TIM

The standard PsyQ image format: what the original passes to `GsGetTimInfo` and uploads to VRAM with `LoadImage`.

| Offset | Type | Field |
|---|---|---|
| 0 | `u32` | Magic `0x10` |
| 4 | `u32` | Flags: bits 0-2 pixel mode (0 = 4 bits, 1 = 8 bits, 2 = 16 bits, 3 = 24 bits), bit 3 has a CLUT |
| 8 | block | CLUT, only if bit 3 is set |
| … | block | Image |

Each block is `{u32 length; u16 x, y, width, height; data}`. The length includes the 12 header bytes. `x` and
the width count 16-bit VRAM units, not pixels: one unit is 4 pixels at 4 bits, 2 at 8 bits, 1 at 16
bits and 2/3 at 24 bits (what is left over in each row is padding). The data takes `width × 2 × height` bytes.

- **CLUT:** `width × height` 16-bit colors. A TIM can have several palettes: one per row or, if the row is wider
  than the palette (16 colors at 4 bits, 256 at 8 bits), several one after another in the same row. `timPaletteCount`
  counts them and they are numbered left to right and top to bottom; `decodeTim(tim, n)` uses palette `n`. If the
  row is shorter than the full palette, the palette is the whole row, and an index that falls outside it is an error.
- **Pixels:** at 4 bits the first pixel of each byte is in the low nibble. At 24 bits each pixel is three bytes
  R, G, B.
- **What is rejected:** a magic other than `0x10`, mode 4 (mixed) or higher, empty blocks or blocks larger than VRAM
  (1024 × 512 units), a length smaller than what width × height require, and any block that goes past the end.
  Whatever comes after the last block is ignored; `Tim::byteSize` says how many bytes it took according to its lengths.
- **The image block is found using the CLUT block's length**, as `GsGetTimInfo` does.
- **A 4- or 8-bit TIM without a CLUT** is valid (its palette is elsewhere in VRAM): its indices are read, but it
  cannot be converted to RGBA.

### Colors and transparency

A console color (`Color15`) has 5 bits per channel (red in bits 0-4, green in 5-9, blue in 10-14) and the STP bit
(15). When converting to RGBA8 each channel is widened by repeating its high bits (`v << 3 | v >> 2`, so 31 gives
255).

The alpha rule (`toRgba8`):

- `0x0000` is the only transparent color (alpha 0): the console does not draw those texels.
- Everything else is opaque (alpha 255), including `0x8000`, which is opaque black.
- The STP bit only matters when the primitive is drawn semi-transparent, and that is decided by whoever draws, not
  by the image. That is why it does not change alpha, but it is not lost: `Tim` stores the raw CLUT and pixels.
- At 24 bits there is no transparency.

## Stacked TIMs

`ETCDAT\ETCTIM.BIN` and `STDDAT\STDTIM.BIN` are TIMs one after another with no index. `readStackedTims` walks them like
`utils2.c: loadStackedTIMFile`:

- The list ends at the first word that is not `0x10` (`StackedTims::end`), or at the end of the file.
- Each TIM advances by what its width × height give, not its lengths: `4 + ((width × height / 2) + 4) × 4` for the
  magic, flags and image, plus `((width × height / 2) + 3) × 4` for the CLUT if there is one, with the sizes in
  16-bit units. The division rounds down, as in the original.
- If that does not match what the block lengths say, the original's size is used and the result carries a warning in
  `warnings` (the reader does not write to the log).
- Each TIM is read with the normal reader from its offset to the end of the file, so its data comes out the same way
  the original uploads it. If a TIM is malformed or the stride goes past the file, it is an error.

## Files with several TIMs

A loose `.TIM` can have more than one TIM: `STDDAT\T_TOGI.TIM` has four, the textures of an arena that the
original uploads at once with a fixed number of TIMs (`std_setup.c: STD_loadArenaTIMToVRAM`). `readTimSequence` reads
these files, and also single-TIM ones: each TIM starts where the previous one's block lengths end, and
the list ends at the first word that is not `0x10`. Unlike stacked TIMs, the width × height count is not used,
because loose TIMs are read with their lengths and one with an odd width × height would end 2 bytes earlier.

## Fixed-stride packs

Some files are entries concatenated at a fixed stride of sectors (2048 bytes), with no header. The stride and the
number of entries are in the original's code, so `knownPacks()` declares them. `splitPack` splits any pack by
its stride and `splitKnownPack` also checks the number of entries.

| File | Stride | Entries | Index | Code |
|---|---|---|---|---|
| `CHDAT\ALLTIM.TIM` | 9 sectors (`0x4800`) | 180 | Digimon type | `model.c: loadDigimonTexture, loadMMDAsync, uploadModelTexture` |
| `CARD\CARD.ALL` | 14 sectors (`0x7000`) | 66 | card id | `game_menu.c: loadCardImage`, `file.c: loadStackedTIMEntry` |

- **What is in each entry:** in both packs, a TIM at the start of the entry and padding up to the stride. The
  original reads the entry's sectors (`readFileSectors(file, buffer, index × stride, stride)`) and passes to
  `GsGetTimInfo` the word that follows the magic, without checking it.
- **ALLTIM:** the original does not use the TIM's VRAM position: `uploadModelTexture` replaces it with the texture
  page and the CLUT of the model that uses it. The 180 entries are the Digimon types (`loadMMDAsync` rejects
  `digimonType >= 0xB4`).
- **CARD.ALL:** the TIM is uploaded at its own position. The 66 entries are the cards (`CARD_DATA` takes `0x108`
  bytes of 4-byte entries).
- **Sizes:** in the file table of all versions (`src/main/file_table_*.c`) ALLTIM.TIM takes
  `0x32A000` bytes (180 × `0x4800`) and CARD.ALL `0x1CE000` (66 × `0x7000`).

## PNG

`encodePngRgba` and `encodePngIndexed` write the extractor's PNGs with [lodepng](https://github.com/lvandeve/lodepng)
(vcpkg package, linked privately). The output is deterministic: the same pixels give the same bytes with the same
library version, because no date or text is written and automatic color type conversion is turned off.

- **RGBA:** 8-bit-per-channel PNG.
- **Indexed:** PNG with an 8-bit palette; the palette goes in `PLTE` and, if any color is not opaque, its alpha in
  `tRNS`. 4-bit TIMs are also written at 8 bits per pixel.

**What the extractor uses by default:** 4- and 8-bit TIMs with a CLUT are written as an indexed PNG with palette 0,
because nothing is lost this way: the indices and the palette stay in the file, the RGBA can be derived from them (not
the other way round, since a palette often repeats colors) and any image editor opens it. The other palettes of a TIM
with several go in its metadata YAML, with the STP bit of each color. 16- and 24-bit TIMs go as RGBA
([`dwx extract`](extract.md)).
