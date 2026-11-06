#include <dwe/formats/video/mdec.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

#include "video_fixtures.hpp"

using namespace dwe::formats::video;
using fixtures::BitWriter;
using Codes = std::vector<std::uint16_t>;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

std::uint16_t code(unsigned top6, int bottom10) {
  return static_cast<std::uint16_t>((top6 << 10U) | (static_cast<unsigned>(bottom10) & 0x3FFU));
}

// Writes the DC size codes of one macroblock whose blocks all have a zero difference, each block
// ended at once.
void writeEmptyMacroblock(BitWriter& bits) {
  for (int block = 0; block < 6; ++block) {
    bits.bits(block < 2 ? "00" : "100");
    bits.bits("10");
  }
}

// The sample a DC-only block of this level decodes to, plus 128.
int dcSample(int level) {
  std::array<std::int16_t, 64> coefficients{};
  int bias = 0;
  if (level != 0) {
    bias = level < 0 ? 8 : -8;
  }
  coefficients[0] = static_cast<std::int16_t>((level * 32) + bias);
  return inverseDct(coefficients)[0] + 128;
}

} // namespace

TEST_CASE("version 3 DC codes are differences from the previous block of the same kind") {
  BitWriter bits;
  bits.bits("110 101"); // Cr: size 3, +5 -> 20
  bits.bits("10");
  bits.bits("01 0"); // Cb: size 1, -1 -> -4
  bits.bits("10");
  bits.bits("100"); // Y0: size 0 -> 0
  bits.bits("10");
  bits.bits("00 1"); // Y1: size 1, +1 -> 4
  bits.bits("10");
  bits.bits("01 01"); // Y2: size 2, -2 -> -4
  bits.bits("10");
  bits.bits("1111110 10000000"); // Y3: size 8, +128 -> 508
  bits.bits("10");
  const auto stream = fixtures::makeBitstream(3, 7, 12, bits);
  const auto codes = decodeBitstream(stream, 1);
  REQUIRE_MESSAGE(codes.has_value(), errorOf(codes));
  const Codes expected = {code(7, 20), MdecEndOfBlock, code(7, -4),  MdecEndOfBlock,
                          code(7, 0),  MdecEndOfBlock, code(7, 4),   MdecEndOfBlock,
                          code(7, -4), MdecEndOfBlock, code(7, 508), MdecEndOfBlock};
  CHECK(*codes == expected);
}

TEST_CASE("the DC predictors carry over to the next macroblock") {
  BitWriter bits;
  for (int macroblock = 0; macroblock < 2; ++macroblock) {
    bits.bits("01 1 10"); // Cr +1 -> 4, then 8
    bits.bits("00 10");   // Cb 0
    for (int y = 0; y < 4; ++y) {
      bits.bits("00 1 10"); // Y +1 each block
    }
  }
  const auto codes = decodeBitstream(fixtures::makeBitstream(3, 1, 24, bits), 2);
  REQUIRE_MESSAGE(codes.has_value(), errorOf(codes));
  CHECK((*codes)[12] == code(1, 8));
  CHECK((*codes)[16] == code(1, 20)); // Y0 of the second macroblock follows Y3 (16) of the first
  CHECK((*codes)[22] == code(1, 32));
}

TEST_CASE("version 2 stores each DC in 10 bits") {
  BitWriter bits;
  for (int block = 0; block < 6; ++block) {
    bits.value(static_cast<std::uint32_t>(-3 * block) & 0x3FFU, 10);
    bits.bits("10");
  }
  const auto codes = decodeBitstream(fixtures::makeBitstream(2, 2, 12, bits), 1);
  REQUIRE_MESSAGE(codes.has_value(), errorOf(codes));
  CHECK((*codes)[10] == code(2, -15));
}

TEST_CASE("AC codes give runs and signed levels, the end-of-block code ends the block") {
  BitWriter bits;
  bits.bits("00");                 // Cr DC 0
  bits.bits("11 0");               // run 0, +1
  bits.bits("011 1");              // run 1, -1
  bits.bits("00101 0");            // run 0, +3
  bits.bits("0000000010000 1");    // run 10, -2
  bits.bits("000000000011111 0");  // run 1, +8 (a 15-bit code)
  bits.bits("0000000000010000 1"); // run 1, -18 (a 16-bit code)
  bits.bits("10");
  for (int block = 1; block < 6; ++block) {
    bits.bits(block < 2 ? "00 10" : "100 10");
  }
  const auto codes = decodeBitstream(fixtures::makeBitstream(3, 1, 18, bits), 1);
  REQUIRE_MESSAGE(codes.has_value(), errorOf(codes));
  const Codes first(codes->begin(), codes->begin() + 8);
  const Codes expected = {code(1, 0),   code(0, 1), code(1, -1),  code(0, 3),
                          code(10, -2), code(1, 8), code(1, -18), MdecEndOfBlock};
  CHECK(first == expected);
}

TEST_CASE("an escape code carries a 6-bit run and a 10-bit level") {
  BitWriter bits;
  bits.bits("00");
  bits.bits("000001");
  bits.value(5, 6);
  bits.value(static_cast<std::uint32_t>(-300) & 0x3FFU, 10);
  bits.bits("10");
  for (int block = 1; block < 6; ++block) {
    bits.bits(block < 2 ? "00 10" : "100 10");
  }
  const auto codes = decodeBitstream(fixtures::makeBitstream(3, 1, 13, bits), 1);
  REQUIRE_MESSAGE(codes.has_value(), errorOf(codes));
  CHECK((*codes)[1] == code(5, -300));
}

TEST_CASE("malformed bitstreams are errors") {
  BitWriter empty;
  writeEmptyMacroblock(empty);
  const auto good = fixtures::makeBitstream(3, 1, 12, empty);
  REQUIRE(decodeBitstream(good, 1).has_value());

  SUBCASE("header") {
    auto stream = good;
    stream[3] = std::byte{0}; // the 0x3800 marker
    CHECK_FALSE(decodeBitstream(stream, 1).has_value());
    CHECK_FALSE(decodeBitstream(fixtures::makeBitstream(4, 1, 12, empty), 1).has_value());
    CHECK_FALSE(decodeBitstream(fixtures::makeBitstream(3, 0, 12, empty), 1).has_value());
    CHECK_FALSE(decodeBitstream(fixtures::makeBitstream(3, 64, 12, empty), 1).has_value());
    CHECK_FALSE(decodeBitstream(std::span{good}.first(6), 1).has_value());
  }
  SUBCASE("a code count that does not match the header") {
    CHECK_FALSE(decodeBitstream(fixtures::makeBitstream(3, 1, 80, empty), 1).has_value());
  }
  SUBCASE("more macroblocks than the bitstream holds") {
    CHECK_FALSE(decodeBitstream(good, 2).has_value());
  }
  SUBCASE("an unknown AC code") {
    BitWriter bits;
    bits.bits("00 0000000000000000");
    CHECK_FALSE(decodeBitstream(fixtures::makeBitstream(3, 1, 2, bits), 1).has_value());
  }
  SUBCASE("an unknown DC code") {
    BitWriter bits;
    bits.bits("11111111");
    CHECK_FALSE(decodeBitstream(fixtures::makeBitstream(3, 1, 2, bits), 1).has_value());
  }
  SUBCASE("a block with more than 64 coefficients") {
    BitWriter bits;
    bits.bits("00");
    for (int i = 0; i < 2; ++i) {
      bits.bits("000001");
      bits.value(40, 6);
      bits.value(1, 10);
    }
    bits.bits("10");
    CHECK_FALSE(decodeBitstream(fixtures::makeBitstream(3, 1, 4, bits), 1).has_value());
  }
  SUBCASE("a DC that leaves the 10-bit range") {
    BitWriter bits;
    bits.bits("00 10 00 10");
    bits.bits("1111110 11111111 10"); // +255 * 4
    bits.bits("1111110 11111111 10"); // and again: 2040
    bits.bits("100 10 100 10");
    CHECK_FALSE(decodeBitstream(fixtures::makeBitstream(3, 1, 12, bits), 1).has_value());
  }
}

TEST_CASE("a frame is made of whole 16x16 macroblocks") {
  CHECK(countMacroblocks(320, 240).value_or(0) == 300);
  CHECK(countMacroblocks(16, 16).value_or(0) == 1);
  CHECK_FALSE(countMacroblocks(0, 16).has_value());
  CHECK_FALSE(countMacroblocks(16, 0).has_value());
  CHECK_FALSE(countMacroblocks(24, 16).has_value());
  CHECK_FALSE(countMacroblocks(320, 232).has_value());
  // A 24x16 frame would round down to the one macroblock this bitstream holds.
  BitWriter bits;
  writeEmptyMacroblock(bits);
  const auto stream = fixtures::makeBitstream(3, 1, 12, bits);
  CHECK(decodeFrame(stream, 16, 16).has_value());
  CHECK_FALSE(decodeFrame(stream, 24, 16).has_value());
}

TEST_CASE("the IDCT table is libpress's formula, rounded down") {
  for (std::size_t u = 0; u < 8; ++u) {
    for (std::size_t x = 0; x < 8; ++x) {
      const double scale = u == 0 ? std::numbers::sqrt2 / 2.0 : 1.0;
      const double angle = static_cast<double>(((2 * x) + 1) * u) * std::numbers::pi / 16.0;
      const double exact = 32768.0 * scale * std::cos(angle);
      const double stored = IdctTable[(u * 8) + x];
      CAPTURE(u);
      CAPTURE(x);
      CHECK(stored <= exact);
      CHECK(exact - stored < 1.0);
    }
  }
}

TEST_CASE("the IDCT of a DC-only block is flat") {
  std::array<std::int16_t, 64> coefficients{};
  coefficients[0] = 3192; // level 100 dequantised: 100 * 2 * 16 - 8
  const auto samples = inverseDct(coefficients);
  CHECK(std::ranges::all_of(samples, [](std::int8_t sample) { return sample == 25; }));
  CHECK(std::ranges::all_of(inverseDct({}), [](std::int8_t sample) { return sample == 0; }));
}

TEST_CASE("the IDCT of the first horizontal frequency is a falling half cosine") {
  std::array<std::int16_t, 64> coefficients{};
  coefficients[1] = 1600;
  const auto samples = inverseDct(coefficients);
  for (std::size_t row = 0; row < 8; ++row) {
    for (std::size_t column = 0; column < 4; ++column) {
      CHECK(samples[(row * 8) + column] == samples[column]);
      CHECK(samples[(row * 8) + column] == -samples[(row * 8) + 7 - column]);
    }
  }
  CHECK(samples[0] > samples[1]);
  CHECK(samples[1] > samples[2]);
  CHECK(samples[2] > samples[3]);
  CHECK(samples[3] > 0);
}

TEST_CASE("the IDCT saturates to a signed byte after wrapping to 9 bits") {
  std::array<std::int16_t, 64> coefficients{};
  coefficients[0] = 0x3FFF;
  CHECK(inverseDct(coefficients)[0] == 127);
  coefficients[0] = -0x4000;
  CHECK(inverseDct(coefficients)[0] == -128);
  // 312 before saturation: 9 bits make it -200, which saturates to -128.
  for (const std::size_t index : std::array<std::size_t, 4>{0, 1, 8, 9}) {
    coefficients[index] = 7000;
  }
  CHECK(inverseDct(coefficients)[0] == -128);
}

TEST_CASE("macroblocks fill the frame in columns, blocks in Cr, Cb, Y0..Y3 order") {
  const std::array levels = {100, -100, 0, 200, -60, 30};
  Codes codes;
  for (int macroblock = 0; macroblock < 3; ++macroblock) {
    for (const int level : levels) {
      codes.push_back(code(1, level + (macroblock * 4)));
      codes.push_back(MdecEndOfBlock);
    }
  }
  const auto frame = decodeMacroblocks(codes, 32, 32);
  CHECK_FALSE(frame.has_value()); // four macroblocks needed
  codes.insert(codes.end(), codes.begin(), codes.begin() + 12);
  const auto full = decodeMacroblocks(codes, 32, 32);
  REQUIRE_MESSAGE(full.has_value(), errorOf(full));
  CHECK(full->cr[0] == dcSample(100));
  CHECK(full->cb[0] == dcSample(-100));
  CHECK(full->y[0] == dcSample(0));
  CHECK(full->y[8] == dcSample(200));
  CHECK(full->y[std::size_t{8} * 32] == dcSample(-60));
  CHECK(full->y[(8 * 32) + 8] == dcSample(30));
  // The second macroblock is below the first; the third starts the second column.
  CHECK(full->y[std::size_t{16} * 32] == dcSample(4));
  CHECK(full->cr[std::size_t{8} * 16] == dcSample(104));
  CHECK(full->y[16] == dcSample(8));
  CHECK(full->cr[8] == dcSample(108));
}

TEST_CASE("the MDEC skips padding codes and ends a block at 64 coefficients") {
  Codes codes = {MdecEndOfBlock, code(1, 0), code(62, 5), code(0, 7)};
  for (int block = 1; block < 6; ++block) {
    codes.push_back(code(1, 0));
    codes.push_back(MdecEndOfBlock);
  }
  const auto frame = decodeMacroblocks(codes, 16, 16);
  REQUIRE_MESSAGE(frame.has_value(), errorOf(frame));
  CHECK_FALSE(decodeMacroblocks(codes, 16, 8).has_value());
  CHECK_FALSE(decodeMacroblocks(Codes{code(1, 0)}, 16, 16).has_value());
}

TEST_CASE("a bitstream decodes to the planes of its MDEC codes") {
  BitWriter bits;
  bits.bits("110 101 11 0 10");
  bits.bits("01 0 011 1 10");
  for (int y = 0; y < 4; ++y) {
    bits.bits("00 1 00101 0 10");
  }
  const auto stream = fixtures::makeBitstream(3, 4, 18, bits);
  const auto codes = decodeBitstream(stream, 1);
  REQUIRE_MESSAGE(codes.has_value(), errorOf(codes));
  const auto direct = decodeMacroblocks(*codes, 16, 16);
  REQUIRE_MESSAGE(direct.has_value(), errorOf(direct));
  const auto frame = decodeFrame(stream, 16, 16);
  REQUIRE_MESSAGE(frame.has_value(), errorOf(frame));
  CHECK(frame->y == direct->y);
  CHECK(frame->cb == direct->cb);
  CHECK(frame->cr == direct->cr);
  CHECK_FALSE(decodeFrame(stream, 15, 16).has_value());
}

TEST_CASE("YCbCr converts to RGB with the MDEC's fixed-point formulas") {
  CHECK(toRgb(128, 128, 128) == Rgb8{.r = 128, .g = 128, .b = 128});
  CHECK(toRgb(255, 128, 128) == Rgb8{.r = 255, .g = 255, .b = 255});
  CHECK(toRgb(0, 128, 128) == Rgb8{.r = 0, .g = 0, .b = 0});
  CHECK(toRgb(128, 128, 255) == Rgb8{.r = 255, .g = 37, .b = 128});
  CHECK(toRgb(128, 255, 128) == Rgb8{.r = 128, .g = 84, .b = 255});
}
