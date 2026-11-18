# Video: `MOVIE/*.STR`

The four movies on the disc (`MOVIE/OP1.STR`, `OP2.STR`, `ED2.STR`, `EDR.STR`) are STR files: mode 2 sectors that
the original reads at a fixed speed, with MDEC video and XA audio interleaved. `formats/video` separates them and
decodes them in full (video and audio) without new dependencies; `dwx`'s `movie` extractor writes each movie
as YUV4MPEG2 + WAV + YAML.

The original plays them in the `MOV_REL.BIN` overlay (`mov.c`), which is loaded at `0x80010000`
(`utils2.c: playMovie` → `loadDynamicLibrary`): `MOV_startStream` reads at double speed
(`CdRead2(CdlModeStream | CdlModeSpeed | CdlModeRT | CdlModeSize1)`), `DecDCTvlc2` turns the bitstream into MDEC codes and
`DecDCTin(..., 3)` outputs 24-bit color in 16-pixel strips (`MOV_onSliceDecoded`).

## Sectors

An STR has to be read as **raw sectors with their subheader** (a 2352-byte image). The 2048-byte view
of an ISO loses the audio (form 2, 2324 bytes) and the sector types; with such an image the extractor fails with
an error that says so. `disc::SectorData::subheader` stores `{file, channel, submode, coding}` of each mode 2 sector
of a raw image.

| Type | Subheader | Content |
|---|---|---|
| Video | form 1, `submode` with the data bit (`0x08`) or video bit (`0x02`); on the disc `0x48` | `StHEADER` of 0x20 bytes and 0x7E0 bytes of the frame's bitstream |
| Audio | form 2, audio bit (`0x04`); on the disc `0x64`, the last one `0xE4` (end of file) | 18 XA sound groups of 128 bytes and 20 unused bytes |
| Padding | no type bit (only `0x01`/`0x80` at most) and 2048 zeros | nothing; 3 to 5 at the end of each file |

Any other sector is a reader error and the extractor counts it in `unknowns`. In the four movies the pattern is
strict: 7 video sectors and 1 audio sector.

`StHEADER` (libpress), little-endian:

| Offset | Field | Check |
|---|---|---|
| 0x00 | `u16 0x0160`, `u16 0x8001` | marker |
| 0x04 | `u16 sectorInFrame`, `u16 sectorsInFrame` | `sectorInFrame < sectorsInFrame` |
| 0x08 | `u32 frameNumber` | from 1, consecutive |
| 0x0C | `u32 frameBytes` | fits in its sectors |
| 0x10 | `u16 width`, `u16 height` | the same in the whole file (320 × 176) |
| 0x14 | `u32 headm`, `u32 headv` | they are the first 8 bytes of the bitstream |
| 0x1C | `u32` reserved | 0 |

`StrFrameAssembler` joins the pieces of each frame in order and checks that all its sectors have the same
header, that the frame fits, that the padding after `frameBytes` is zero and that `headm`/`headv` repeat the start of
the bitstream. A frame that is missing sectors is an error, and a first sector that interrupts another frame starts
the next one.

**Rate.** At double speed 150 sectors go by per second; the audio confirms it (a stereo sector of 2016
samples every 8 sectors is 37,800 samples per second). A frame starts every 5 sectors on average, so it is
**30 fps**. The extractor computes 150 / (sectors between the first and the last frame start / frames − 1) and, if the
period is within 0.05 sectors of an integer, rounds it; otherwise, it leaves the reduced fraction.

## Bitstream (`DecDCTvlc2`)

An 8-byte header: `u16` number of MDEC codes (half, rounded up to 32), `u16 0x3800`, `u16`
quantization scale (1..63, the same for the whole frame) and `u16` version (2 or 3; the movies are version 3). Bits
are read from 16-bit little-endian words, from most significant to least.

Each 16 × 16 macroblock is six blocks in the order Cr, Cb, Y0, Y1, Y2, Y3:

- **DC, version 3:** a size code (MPEG-1's luma B.12 and chroma B.13 tables) and that many bits of
  difference; a first bit of 0 is negative. The difference counts in steps of 4 and is added to the previous one of the same kind (one
  predictor for Y0..Y3, another for Cb and another for Cr), which start at 0 at the beginning of the frame and carry on
  from one macroblock to the next. The result has to fit in −512..511.
- **DC, version 2:** 10 signed bits.
- **AC:** the whole MPEG-1 B.14 table (111 codes, including the 16-bit ones `0000 0000 0001 xxxx`), which is the one
  `DecDCTvlcBuild` builds, with the sign bit after; `10` is end of block and `000001` escape, with 6 bits of
  run and 10 of signed level.

The decoder gives the MDEC codes: `scale << 10 | DC` for the DC, `run << 10 | level` for each AC and `0xFE00`
as end of block. These are errors: an unknown code, a block that goes past 64 coefficients, a bitstream that
ends in the middle of a macroblock or a number of codes that does not match the header. In the four movies the
header matches in every frame.

## MDEC

`decodeMacroblocks` does what the MDEC does with those codes, modeled on Mednafen (`mdec.c`) and checked with the
tables that libpress uploads (`DecDCTReset`):

- **Dequantization.** The DC is multiplied by the first weight of the matrix (2), without the scale; each AC by
  `(level · scale · Q[k]) >> 3`. Both are stored × 16 with a bias of 8 toward zero and saturated to 15 bits. `Q`
  is MPEG-1's default intra matrix with DC 2, in natural order; coefficient k goes to its position by the
  standard zigzag.
- **IDCT.** Matrix `M[x][u] = floor(32768 · c(u) · cos((2x + 1)uπ/16)) >> 3` with `c(0) = 1/√2` and `c(u) = 1` in the
  rest. It is `floor` and not rounding: that is what the executable's table is. It is written in the code (`IdctTable`, the one
  psx-spx lists for the MDEC command that loads it) instead of being computed with `cos`, so as not to depend on each
  platform's libm; a test compares it with the formula. First columns, then rows, each pass with
  `(sum + 0x4000) >> 15`; the result stays at 9 bits and is saturated to a signed byte.
- **Planes.** Signed samples are stored + 128 as unsigned bytes. Macroblocks fill the frame by
  columns: top to bottom and then left to right.

**Why this IDCT.** It is the hardware's: integer, deterministic, the same on any platform, and it is what the
original sees before its RGB step. `decodeMacroblocks` keeps it for tests and for comparing with the original,
but it loses detail along the way: the table rounds down, each pass rounds, the result stays at 8 bits
and an out-of-range value wraps around in 9 bits before saturating. Dequantization also truncates (`>> 3` and the bias
of 8 toward zero).

**Precise decoding** (an engine improvement; `dwx` uses it when writing the video). `decodePreciseFrame` reads the same
bitstream with the same matrix and the same scale, but without losing anything before the IDCT:

- **Exact dequantization.** Coefficients are stored × 8, so `level · scale · Q[k]` (the AC) and
  `level · Q[0] · 8` (the DC) are exact integers: no `>> 3`, no bias and no saturation to 15 bits.
- **Exact integer IDCT.** Basis `round(2^24 · c(u)/2 · cos((2x + 1)uπ/16))` (`PreciseIdctTable`, written in the
  code and compared with the formula in a test), 64-bit sums, columns `>> 12` and rows `>> 35`, both
  rounded. It is the ideal IDCT to within a thousandth of a level and is still integer: the same bitstream gives the
  same samples on any platform.
- **More bits.** Samples are stored in sixteenths of a level (`PreciseOne = 16`, from 0 to `PreciseMax = 4080`) and
  are saturated, never wrapped around. Compared with the hardware decoding, within one level on each sample.

**Color.** `toRgb` is the MDEC conversion (Mednafen): `r = y + ((359 · cr + 0x80) >> 8)`,
`b = y + ((454 · cb + 0x80) >> 8)` and in green each product loses low bits
(`(−88 · cb) & ~0x1F` and `(−183 · cr) & ~0x07`) before adding; each channel in 9 bits saturated and + 128. It is used to
check frames as RGB; the video is written in YCbCr, without going through it. A player that uses the full-range
BT.601 matrix gives colors within one level of difference, except green in some cases.

## XA audio (ADPCM)

Each audio sector has 18 sound groups of 128 bytes: 16 of header (the units' parameters in bytes 4..11, with copies
in 0..3 and 12..15) and 112 of samples. With 4 bits they are 8 units of 28 samples (sample n of
unit u at byte `16 + 4n + u/2`, low nibble for even u); with 8 bits, 4 units (byte `16 + 4n + u`).

- A unit's parameter: the low 4 bits are the shift (13..15 are taken as 9), bits 4..5 the filter.
- Sample: `(nibble << 12) >> shift` (or `byte << 8`) plus the prediction
  `(prev · K0 + prev2 · K1 + 32) >> 6` with `K0/K1` = (0, 0), (60, 0), (115, −52), (98, −55), saturated to 16 bits.
- In stereo the units alternate left and right, and each channel keeps its history from one group and from one sector
  to the next.
- `coding`: bit 0 stereo, bit 2 half rate (18,900 Hz), bit 4 8 bits; bit 6 (emphasis) and the reserved values
  are an error.

The rounding is psx-spx's (+32 before the sum's shift). Mednafen shifts each term separately and
may give a difference of one unit in some sample; neither reference is the measured hardware, so the documented one
is chosen. A parameter different from its copy is counted (the one in 4..11 is used) and the extractor puts it in `unknowns`;
on the US disc there are none.

The four movies are stereo at 37,800 Hz and 4 bits, with a single channel (`file 1`, `channel 1`); the original does not
filter channels (`mov.c` does not use `CdlSetfilter`).

## `MOV_MOVIES`

A table of 4 entries of 0x28 bytes in `MOV_REL.BIN` (`mov.c`): `char path[0x20]` (`"\MOVIE\OP1.STR;1"`), `s32
startFrame`, `s32 frameCount`. `MOV_readFrame` stops reading when the frame number reaches `frameCount` or after 2000
empty reads, so the movie that is seen can end before the file. Addresses: US `0x8001094C`, EU
`0x80010B64`, JP and JP rev 1 `0x80010B70` (`movieTableAddress`).

## Restoration

An engine improvement (`packages/formats/src/video/restore.cpp`): `dwx` cleans each frame decoded with precision
before writing it. The movies are heavily quantized (scale 2 to 12 depending on the frame) and the edges of the
8 × 8 blocks show in gradients and skies, along with halos around strong edges. The MDEC scale has the meaning
of an MPEG quantizer (an AC step of `scale · weight / 8`), so MPEG-4's post-filters
(ISO/IEC 14496-2, annex F.3) work as they are, with the frame's scale as `QP`:

- **Deblocking (`deblock`).** Each line of ten samples that crosses a block edge (five on each side), first
  the vertical edges and then the horizontal ones, in the three planes. If the line is flat (at least 6 of its 9
  differences are 2 levels or less) and its range is less than `2 · QP`, a 9-tap low-pass
  `{1, 1, 2, 2, 4, 2, 2, 1, 1} / 16` spreads the step. Otherwise, only the step of the edge itself is reduced (its
  third-order term) down to the smaller of its neighbors, when it is smaller than `QP`, and never more than half the step. A
  real edge of the image, larger than what quantization explains, is not touched.
- **Deringing (`dering`).** Inside each block with a range of at least 16 levels, the samples on one side of the
  block's mean level are smoothed with `[1 2 1; 2 4 2; 1 2 1] / 16` only if their 9 neighbors are on the same side (away
  from the edge), and none moves more than `QP / 2`.

All in integer arithmetic: the same frame always gives the same samples. There is no temporal filter: the frames are already
real at 30 fps (few repeated) and a filter between frames would blur the fast motion of the movies.

## `dwx` output

| File | Content |
|---|---|
| `movie/<name>/video.y4m` | YUV4MPEG2 `C420p10` (10 bits, each sample in 2 little-endian bytes), `XCOLORRANGE=FULL`, `Ip`, `A1:1`, at the real rate (`F30:1`): each frame is the Y, Cb and Cr planes of the precise decoding, already restored |
| `movie/<name>/audio.wav` | 16-bit little-endian PCM, the XA's channels and rate |
| `movie/<name>/movie.yaml` | `source`, `video` (size, frames, first frame, fps, bitstream version, `chroma: 420p10`, `range: full`), `audio` (rate, channels, samples, XA bits) or `null`, `sectors` (total, video, audio, padding) and `playback` (index, path, `start_frame` and `frame_count` from `MOV_MOVIES`) or `null` if the version is not known. Schema: `content/schemas/movie.schema.json` |

- **Chroma.** The MDEC uses the same chroma sample for the 4 pixels of its 2 × 2 square (nearest neighbor), so
  the sample is at the center of the square, as in `C420jpeg`. The planes are full range: luma from 0 to
  1023 (`level · 1023 / 255`, rounded) and chroma centered on 512 with the same factor, as H.273 scales the full
  range to 10 bits. The 10 bits keep the sixteenths of a level of the precise decoding that 8 bits
  would round off, and avoid banding in dark gradients.
- **Writing in parts.** `OutputFile::stream` (extractor.hpp) is an output that the base package requests in pieces:
  the extractor stores only the bitstreams (about 35 MB per movie) and the video is decoded frame by frame when
  written, so it is never whole in memory (about 300 MB per movie). The bitstreams and WAVs of the four
  movies (about 200 MB) do stay in memory until the package is written. Outputs in parts do not take part in the
  duplicate search.
- **Coverage.** An STR is `extracted` when each sector is video of a frame that decodes, audio of the movie's
  stream or padding. Everything else (a sector that is not recognized, a frame that does not decode, whose size is not a multiple of
  16 × 16 macroblocks or that is missing sectors, audio of another channel, an XA parameter different from its copy, an unreadable `MOV_MOVIES`) goes to `unknowns`; if
  there are more than 32 problems, the rest are counted in one.

To view or re-encode it: `ffmpeg -i video.y4m -i audio.wav ...` (outside the project).

## Cooked movie (`dwx cook`)

| File | Content |
|---|---|
| `movie/<name>/video.ivf` | 10-bit 4:2:0 AV1 (8 bits if the Y4M is 8), full range, BT.601 matrix, in an IVF: a 32-byte header (`DKIF`, version 0, header size 32, `AV01`, width and height `u16`, rate and scale `u32` at 16 and 20, number of frames `u32` at 24) and, per frame, 12 bytes (size `u32`, `pts` `u64`) and its data. One IVF frame per Y4M frame, at the same size and rate |
| `movie/<name>/audio.opus` | The WAV run through the sound cooker ([Opus](opus.md)): 48 kHz, the same channels |
| `movie/<name>/movie.yaml` | Copied: it describes the movie as it was extracted (the engine takes the rate from the decoded sound) |

The cooker (`apps/dwx/src/video_cooker.cpp`, `video` cooker version 2) encodes with libaom in good-quality mode with
constant quality (`cq-level` 14) and fixed settings and one thread, so the same Y4M always gives the same
bytes. It reads 8-bit (`C420jpeg`) and 10-bit (`C420p10`) Y4M; another depth is rejected.
`assets::readIvfHeader` checks the header (`dwx validate`, `dwe-game --check-content`) and `video::Av1Reader`
decodes it frame by frame. The reasons and measurements are in [movies](../architecture/movies.md#cooked-format-and-later-work).

`packages/formats/src/video/mdec.cpp` and `restore.cpp` are compiled optimized in debug builds too (like
`sha1.cpp`): without that, the extraction in `ci` (debug with sanitizers) took almost 9 minutes with the hardware
decoding. When extracting, each frame only goes through the VLC stage (`decodeBitstream`, which checks its bitstream and its
size); precise decoding and restoration run only once, when writing the Y4M. With them, the whole
extraction in `ci` goes from about 6 minutes to about 16 (measured on 2026-11-08 on a shared machine); an
optimized `dwx` build takes much less.

## Tests

- `packages/formats/tests/mdec_tests.cpp`, `xa_adpcm_tests.cpp`, `str_tests.cpp` and `movie_tests.cpp`: synthetic bitstreams and sectors
  (DC, predictors, version 2, 15- and 16-bit AC, escape, end of block, IDCT, precise IDCT, macroblocks,
  all XA filters, stereo, 8 bits, 8-sector interleaving, malformed inputs, 8- and 10-bit Y4M, WAV,
  `MOV_MOVIES` and schema). `restore_tests.cpp`: deblocking evens out a small step and respects a real edge, its
  strength follows the scale, deringing moves nothing more than half a scale and everything is deterministic.
- `apps/dwx/tests/video_extractors_tests.cpp`: a synthetic raw image with an STR.
- With `DWE_DISC`: `packages/formats/tests/video_real_disc_tests.cpp` decodes the first frames and the audio of each
  movie, and `apps/dwx/tests/video_real_disc_tests.cpp` extracts all four (no `unknowns`) and decodes the
  shortest one in full without writing anything. Decoding all four takes minutes in a build with sanitizers; the
  whole base package test (`real_disc_tests.cpp`) does it when writing them.

## Status with the US disc

The four movies come out `extracted`, with no `unknowns`:

| File | Sectors (video / audio / padding) | Frames | Duration |
|---|---|---|---|
| `MOVIE/ED2.STR` | 13,264 (11,603 / 1,658 / 3) | 2652 | 88.4 s |
| `MOVIE/EDR.STR` | 17,264 (15,103 / 2158 / 3) | 3452 | 115.1 s |
| `MOVIE/OP1.STR` | 18,600 (16,271 / 2325 / 4) | 3719 | 124.0 s |
| `MOVIE/OP2.STR` | 16,336 (14,289 / 2042 / 5) | 3266 | 108.9 s |
