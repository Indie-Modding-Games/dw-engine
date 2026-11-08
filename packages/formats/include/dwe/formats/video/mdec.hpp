#pragma once

#include <dwe/core/error.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// The video frames of STR files: a compressed bitstream that libpress (DecDCTvlc2) turns into the
// 16-bit codes of the console's macroblock decoder (MDEC), which dequantises them, runs an 8x8 IDCT
// and converts YCbCr to RGB. The decoder here repeats both steps with integer arithmetic, so a
// frame decodes to the planes the hardware computes.
namespace dwe::formats::video {

// The code that ends a block in the MDEC's input.
constexpr std::uint16_t MdecEndOfBlock = 0xFE00;
// Size of the header at the start of every frame's bitstream.
constexpr std::size_t BitstreamHeaderSize = 8;

// The header of a frame's bitstream.
struct BitstreamHeader {
  // Half the number of MDEC codes the frame gives, rounded up to a multiple of 32: the 32-bit words
  // DecDCTvlc2 writes.
  std::uint16_t halfCodeCount;
  std::uint16_t quantizationScale; // 1..63, the same for every block of the frame
  std::uint16_t version;           // 2 or 3
};

// Reads and checks the header: the 0x3800 marker, a quantisation scale of 1..63 and version 2 or 3.
core::Result<BitstreamHeader> readBitstreamHeader(std::span<const std::byte> bitstream);

// The number of 16x16 macroblocks in a frame of this size. Fails unless both sides are non-zero
// multiples of 16.
core::Result<std::size_t> countMacroblocks(std::uint32_t width, std::uint32_t height);

// Turns a frame's bitstream into MDEC codes, as DecDCTvlc2 does. Per block, in the order Cr, Cb,
// Y0..Y3 of each macroblock: a DC code (quantisation scale in the top 6 bits, signed DC in the
// bottom 10), one code per run of zeros and level (run in the top 6 bits, signed level in the
// bottom 10) and MdecEndOfBlock. Version 2 stores each DC as 10 bits; version 3 codes it as a
// difference from the previous block of the same kind (Y, Cb or Cr), which starts at 0 at the start
// of the frame. The bitstream must hold exactly macroblockCount macroblocks, no block may pass 64
// coefficients and the number of codes must match the header.
core::Result<std::vector<std::uint16_t>> decodeBitstream(std::span<const std::byte> bitstream,
                                                         std::size_t macroblockCount);

// A decoded frame: full-size luma and half-size chroma (4:2:0), as the MDEC computes them before
// its colour conversion. The hardware keeps the samples as signed bytes; they are stored here plus
// 128, as unsigned bytes.
struct YCbCrFrame {
  std::uint32_t width = 0;      // a multiple of 16
  std::uint32_t height = 0;     // a multiple of 16
  std::vector<std::uint8_t> y;  // width x height
  std::vector<std::uint8_t> cb; // width / 2 x height / 2
  std::vector<std::uint8_t> cr; // width / 2 x height / 2
};

// Runs the MDEC on the codes of a frame of this size: each block is dequantised with the standard
// quantisation matrix, put in natural order (zig-zag), transformed back by the IDCT and placed in
// its macroblock; macroblocks fill the frame in columns, top to bottom and then left to right.
// Fails if the codes end before the last block.
core::Result<YCbCrFrame> decodeMacroblocks(std::span<const std::uint16_t> codes,
                                           std::uint32_t width, std::uint32_t height);

// Decodes a frame's bitstream to its planes: decodeBitstream and then decodeMacroblocks.
core::Result<YCbCrFrame> decodeFrame(std::span<const std::byte> bitstream, std::uint32_t width,
                                     std::uint32_t height);

// The IDCT matrix libpress uploads to the MDEC (DecDCTReset), indexed [frequency * 8 + sample]:
// floor(32768 * c(u) * cos((2x + 1) * u * pi / 16)) with c(0) = 1/sqrt(2) and c(u) = 1 otherwise.
// It is written out rather than computed so the decoder does not depend on the platform's cos; it
// is the table psx-spx lists for the MDEC's "set scale table" command, and the tests check it
// against the formula.
constexpr std::array<std::int16_t, 64> IdctTable = {
    23170, 23170,  23170,  23170,  23170,  23170,  23170,  23170,  //
    32138, 27245,  18204,  6392,   -6393,  -18205, -27246, -32139, //
    30273, 12539,  -12540, -30274, -30274, -12540, 12539,  30273,  //
    27245, -6393,  -32139, -18205, 18204,  32138,  6392,   -27246, //
    23170, -23171, -23171, 23170,  23170,  -23171, -23171, 23170,  //
    18204, -32139, 6392,   27245,  -27246, -6393,  32138,  -18205, //
    12539, -30274, 30273,  -12540, -12540, 30273,  -30274, 12539,  //
    6392,  -18205, 27245,  -32139, 32138,  -27246, 18204,  -6393,  //
};

// The IDCT of one block as the MDEC computes it: coefficients in natural order (row by row),
// columns first, each pass rounded to 15 fractional bits, and the result wrapped to 9 bits and
// clamped to a signed byte.
std::array<std::int8_t, 64> inverseDct(const std::array<std::int16_t, 64>& coefficients);

// The same frames decoded as the encoder meant them, without the hardware's shortcuts: each
// coefficient dequantised exactly (level * scale * weight / 8, no truncation or bias), a
// high-precision IDCT (PreciseIdctTable, 64-bit sums, one rounding at the end) and the result
// saturated, never wrapped. Samples are kept in sixteenths of a level (PreciseOne), so the
// clean-up filters (restore.hpp) and a 10-bit output keep the fractions the 8-bit hardware drops.
// Integer arithmetic throughout: every platform gives the same samples.
inline constexpr int PreciseOne = 16;
inline constexpr int PreciseMax = 255 * PreciseOne;

struct PreciseFrame {
  std::uint32_t width = 0;  // a multiple of 16
  std::uint32_t height = 0; // a multiple of 16
  // The frame's quantisation scale (1..63), which sets how strong the clean-up filters are.
  std::uint16_t quantizationScale = 0;
  std::vector<std::int16_t> y;  // width x height, 0..PreciseMax
  std::vector<std::int16_t> cb; // width / 2 x height / 2, 0..PreciseMax (128 * PreciseOne is grey)
  std::vector<std::int16_t> cr; // width / 2 x height / 2
};

// Decodes a frame's bitstream as decodeFrame does, with the precise arithmetic above.
core::Result<PreciseFrame> decodePreciseFrame(std::span<const std::byte> bitstream,
                                              std::uint32_t width, std::uint32_t height);

// The orthonormal 8-point IDCT basis, indexed [sample * 8 + frequency]:
// round(2^24 * c(u) / 2 * cos((2x + 1) * u * pi / 16)) with c(0) = 1/sqrt(2) and c(u) = 1
// otherwise. Written out, as IdctTable is, so no platform's cos changes it; a test checks it
// against the formula.
constexpr std::array<std::int32_t, 64> PreciseIdctTable = {
    5931642, 8227423,  7750063,  6974873,  5931642,  4660461,  3210181,  1636536,  //
    5931642, 6974873,  3210181,  -1636536, -5931642, -8227423, -7750063, -4660461, //
    5931642, 4660461,  -3210181, -8227423, -5931642, 1636536,  7750063,  6974873,  //
    5931642, 1636536,  -7750063, -4660461, 5931642,  6974873,  -3210181, -8227423, //
    5931642, -1636536, -7750063, 4660461,  5931642,  -6974873, -3210181, 8227423,  //
    5931642, -4660461, -3210181, 8227423,  -5931642, -1636536, 7750063,  -6974873, //
    5931642, -6974873, 3210181,  1636536,  -5931642, 8227423,  -7750063, 4660461,  //
    5931642, -8227423, 7750063,  -6974873, 5931642,  -4660461, 3210181,  -1636536, //
};

// The precise IDCT of one block: coefficients in natural order times 8 (so that every dequantised
// value is a whole number), samples centred on 0 in sixteenths of a level, rounded once and not
// saturated.
std::array<std::int32_t, 64>
preciseInverseDct(const std::array<std::int32_t, 64>& coefficientsTimesEight);

// One pixel of the MDEC's 24-bit output.
struct Rgb8 {
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;

  bool operator==(const Rgb8&) const = default;
};

// Converts one sample of a YCbCrFrame (values plus 128) to RGB with the MDEC's fixed-point formulas
// (full range, coefficients 1.402, -0.344, -0.714 and 1.772).
Rgb8 toRgb(std::uint8_t y, std::uint8_t cb, std::uint8_t cr);

} // namespace dwe::formats::video
