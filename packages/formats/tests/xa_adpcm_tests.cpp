#include <dwe/formats/video/xa_adpcm.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "disc_fixtures.hpp"

using namespace dwe::formats::video;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

constexpr XaCoding MonoFourBit{.channels = 1, .sampleRate = 37800, .bitsPerSample = 4};
constexpr XaCoding StereoFourBit{.channels = 2, .sampleRate = 37800, .bitsPerSample = 4};
constexpr XaCoding MonoEightBit{.channels = 1, .sampleRate = 37800, .bitsPerSample = 8};

// The user data of an audio sector: 18 sound groups, every unit silent with shift 12 and filter 0.
std::vector<std::byte> silentSector() {
  std::vector<std::byte> data(2324);
  for (std::size_t group = 0; group < XaSoundGroups; ++group) {
    for (std::size_t i = 0; i < 16; ++i) {
      data[(group * XaSoundGroupSize) + i] = std::byte{0x0C};
    }
  }
  return data;
}

// Sets a 4-bit unit's parameter (and its copy) and all 28 of its nibbles in one sound group.
void setUnit(std::vector<std::byte>& data, std::size_t group, std::size_t unit,
             std::uint8_t parameter, std::uint8_t nibble) {
  const std::size_t base = group * XaSoundGroupSize;
  fixtures::putU8(data, base + 4 + unit, parameter);
  fixtures::putU8(data, base + (unit < 4 ? unit : unit + 8), parameter);
  for (std::size_t sample = 0; sample < XaSamplesPerUnit; ++sample) {
    const std::size_t offset = base + 16 + (sample * 4) + (unit / 2);
    const auto old = std::to_integer<unsigned>(data[offset]);
    const unsigned value =
        unit % 2 == 0 ? (old & 0xF0U) | nibble : (old & 0x0FU) | (unsigned{nibble} << 4U);
    fixtures::putU8(data, offset, value);
  }
}

// Decodes a mono 4-bit sector whose unit 0 is all 4096 and unit 1 is silent with this filter;
// returns unit 1's first two samples.
std::pair<std::int16_t, std::int16_t> afterStep(std::uint8_t filter) {
  auto data = silentSector();
  setUnit(data, 0, 0, 0x00, 1);
  setUnit(data, 0, 1, static_cast<std::uint8_t>(filter << 4U), 0);
  XaDecoder decoder;
  std::vector<std::int16_t> samples;
  REQUIRE(decoder.decodeSector(data, MonoFourBit, samples).has_value());
  return {samples[28], samples[29]};
}

} // namespace

TEST_CASE("the coding byte gives channels, rate and bits") {
  CHECK(*readXaCoding(0x01) == StereoFourBit);
  CHECK(*readXaCoding(0x00) == MonoFourBit);
  CHECK(readXaCoding(0x04)->sampleRate == 18900);
  CHECK(readXaCoding(0x10)->bitsPerSample == 8);
  CHECK_FALSE(readXaCoding(0x40).has_value());
  CHECK_FALSE(readXaCoding(0x02).has_value());
  CHECK_FALSE(readXaCoding(0x80).has_value());
  CHECK(xaSampleFramesPerSector(StereoFourBit) == 2016);
  CHECK(xaSampleFramesPerSector(MonoFourBit) == 4032);
  CHECK(xaSampleFramesPerSector(MonoEightBit) == 2016);
}

TEST_CASE("a sample is its nibble shifted to the top of 16 bits and right by the shift") {
  auto data = silentSector();
  setUnit(data, 0, 0, 0x00, 7);
  setUnit(data, 0, 1, 0x00, 8);
  setUnit(data, 0, 2, 0x0C, 1);
  setUnit(data, 0, 3, 0x0D, 1); // shift 13 acts as 9
  XaDecoder decoder;
  std::vector<std::int16_t> samples;
  const auto decoded = decoder.decodeSector(data, MonoFourBit, samples);
  REQUIRE_MESSAGE(decoded.has_value(), errorOf(decoded));
  CHECK(*decoded == 0);
  REQUIRE(samples.size() == 4032);
  CHECK(samples[0] == 28672);
  CHECK(samples[27] == 28672);
  CHECK(samples[28] == -32768);
  CHECK(samples[56] == 1);
  CHECK(samples[84] == 8);
  CHECK(samples[112] == 0);
}

TEST_CASE("each filter predicts from the previous two samples") {
  CHECK(afterStep(0) == std::pair<std::int16_t, std::int16_t>{0, 0});
  CHECK(afterStep(1) == std::pair<std::int16_t, std::int16_t>{3840, 3600});
  CHECK(afterStep(2).first == 4032);
  CHECK(afterStep(3).first == 2752);
}

TEST_CASE("predicted samples saturate to 16 bits") {
  auto data = silentSector();
  setUnit(data, 0, 0, 0x00, 7);
  setUnit(data, 0, 1, 0x20, 7);
  XaDecoder decoder;
  std::vector<std::int16_t> samples;
  REQUIRE(decoder.decodeSector(data, MonoFourBit, samples).has_value());
  CHECK(samples[28] == 32767);
}

TEST_CASE("stereo units alternate left and right, each with its own history") {
  auto data = silentSector();
  setUnit(data, 0, 0, 0x00, 1);
  setUnit(data, 0, 1, 0x00, 0xF);
  setUnit(data, 0, 2, 0x10, 0);
  setUnit(data, 0, 3, 0x10, 0);
  XaDecoder decoder;
  std::vector<std::int16_t> samples;
  REQUIRE(decoder.decodeSector(data, StereoFourBit, samples).has_value());
  REQUIRE(samples.size() == 4032);
  CHECK(samples[0] == 4096);
  CHECK(samples[1] == -4096);
  CHECK(samples[56] == 3840);
  CHECK(samples[57] == -3840);
}

TEST_CASE("the history carries over to the next sector") {
  auto first = silentSector();
  setUnit(first, XaSoundGroups - 1, 7, 0x00, 1);
  auto second = silentSector();
  setUnit(second, 0, 0, 0x10, 0);
  XaDecoder decoder;
  std::vector<std::int16_t> samples;
  REQUIRE(decoder.decodeSector(first, MonoFourBit, samples).has_value());
  REQUIRE(decoder.decodeSector(second, MonoFourBit, samples).has_value());
  CHECK(samples[4031] == 4096);
  CHECK(samples[4032] == 3840);
}

TEST_CASE("8-bit units hold one byte per sample") {
  auto data = silentSector();
  for (std::size_t sample = 0; sample < XaSamplesPerUnit; ++sample) {
    fixtures::putU8(data, 16 + (sample * 4), 0x7F);
    fixtures::putU8(data, 16 + (sample * 4) + 1, 0x80);
  }
  for (std::size_t unit = 0; unit < 4; ++unit) {
    fixtures::putU8(data, unit, unit == 1 ? 0x08 : 0x00);
    fixtures::putU8(data, 4 + unit, unit == 1 ? 0x08 : 0x00);
  }
  XaDecoder decoder;
  std::vector<std::int16_t> samples;
  REQUIRE(decoder.decodeSector(data, MonoEightBit, samples).has_value());
  REQUIRE(samples.size() == 2016);
  CHECK(samples[0] == 32512);
  CHECK(samples[28] == -128);
}

TEST_CASE("parameters that differ from their copies are counted; short sectors are errors") {
  auto data = silentSector();
  fixtures::putU8(data, 4, 0x00);
  fixtures::putU8(data, XaSoundGroupSize + 12, 0x01);
  XaDecoder decoder;
  std::vector<std::int16_t> samples;
  const auto mismatches = decoder.decodeSector(data, MonoFourBit, samples);
  REQUIRE(mismatches.has_value());
  CHECK(*mismatches == 2);
  CHECK_FALSE(decoder.decodeSector(std::span{data}.first(2303), MonoFourBit, samples).has_value());
  CHECK_FALSE(decoder
                  .decodeSector(data,
                                XaCoding{.channels = 3, .sampleRate = 37800, .bitsPerSample = 4},
                                samples)
                  .has_value());
}
