#include <dwe/core/yaml.hpp>
#include <dwe/formats/exe/memory_image.hpp>
#include <dwe/formats/video/media_files.hpp>
#include <dwe/formats/video/movie.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "disc_fixtures.hpp"
#include "schema_check.hpp"

using namespace dwe::formats::video;
using dwe::formats::exe::MemoryImage;
using dwe::formats::test::checkAgainstSchema;
using Bytes = std::vector<std::byte>;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

std::string textOf(std::span<const std::byte> bytes) {
  std::string text;
  for (const std::byte value : bytes) {
    text += static_cast<char>(value);
  }
  return text;
}

std::uint32_t u32At(const Bytes& bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8 * i);
  }
  return value;
}

std::uint16_t u16At(const Bytes& bytes, std::size_t offset) {
  return static_cast<std::uint16_t>(std::to_integer<unsigned>(bytes[offset]) |
                                    (std::to_integer<unsigned>(bytes[offset + 1]) << 8U));
}

// A fake overlay with MOV_MOVIES at offset 0x100.
Bytes movieOverlay() {
  Bytes overlay(0x100 + (MovieCount * 0x28));
  const std::array<std::string_view, MovieCount> names = {"\\MOVIE\\A.STR;1", "\\MOVIE\\B.STR;1",
                                                          "\\C.STR;1", "\\MOVIE\\D.STR;1"};
  for (std::size_t i = 0; i < MovieCount; ++i) {
    const std::size_t entry = 0x100 + (i * 0x28);
    fixtures::putText(overlay, entry, names[i]);
    fixtures::putU32(overlay, entry + 0x20, 1);
    fixtures::putU32(overlay, entry + 0x24, static_cast<std::uint32_t>(1000 + i));
  }
  return overlay;
}

MovieInfo sampleInfo() {
  return MovieInfo{.source = "MOVIE/A.STR",
                   .width = 320,
                   .height = 176,
                   .frames = 10,
                   .firstFrame = 1,
                   .fpsNumerator = 30,
                   .fpsDenominator = 1,
                   .bitstreamVersion = 3,
                   .audio = XaCoding{.channels = 2, .sampleRate = 37800, .bitsPerSample = 4},
                   .audioSampleFrames = 4032,
                   .sectors = 80,
                   .videoSectors = 70,
                   .audioSectors = 2,
                   .paddingSectors = 8,
                   .tableIndex = 0,
                   .tableEntry =
                       MovieEntry{.path = "\\MOVIE\\A.STR;1", .startFrame = 1, .frameCount = 9}};
}

} // namespace

TEST_CASE("the Y4M header and frames hold the planes as decoded") {
  const Y4mFormat format{.width = 16, .height = 16, .fpsNumerator = 30, .fpsDenominator = 1};
  CHECK(y4mHeader(format) == "YUV4MPEG2 W16 H16 F30:1 Ip A1:1 C420jpeg XCOLORRANGE=FULL\n");

  const YCbCrFrame frame{.width = 16,
                         .height = 16,
                         .y = std::vector<std::uint8_t>(256, 1),
                         .cb = std::vector<std::uint8_t>(64, 2),
                         .cr = std::vector<std::uint8_t>(64, 3)};
  Bytes out;
  REQUIRE(appendY4mFrame(format, frame, out).has_value());
  REQUIRE(out.size() == 6 + 256 + 128);
  CHECK(textOf(std::span{out}.first(6)) == "FRAME\n");
  CHECK(out[6] == std::byte{1});
  CHECK(out[6 + 256] == std::byte{2});
  CHECK(out[6 + 256 + 64] == std::byte{3});

  YCbCrFrame wrong = frame;
  wrong.cr.pop_back();
  CHECK_FALSE(appendY4mFrame(format, wrong, out).has_value());
  CHECK_FALSE(appendY4mFrame({.width = 32, .height = 16, .fpsNumerator = 30, .fpsDenominator = 1},
                             frame, out)
                  .has_value());
}

TEST_CASE("a WAVE file holds 16-bit PCM after a 44-byte header") {
  const std::vector<std::int16_t> samples = {1, -1, 0x1234, -32768};
  const auto wav = encodeWav(samples, 2, 37800);
  REQUIRE_MESSAGE(wav.has_value(), errorOf(wav));
  REQUIRE(wav->size() == 44 + 8);
  CHECK(textOf(std::span{*wav}.first(4)) == "RIFF");
  CHECK(u32At(*wav, 4) == 44 - 8 + 8);
  CHECK(textOf(std::span{*wav}.subspan(8, 8)) == "WAVEfmt ");
  CHECK(u16At(*wav, 20) == 1);
  CHECK(u16At(*wav, 22) == 2);
  CHECK(u32At(*wav, 24) == 37800);
  CHECK(u32At(*wav, 28) == 37800 * 4);
  CHECK(u16At(*wav, 32) == 4);
  CHECK(u16At(*wav, 34) == 16);
  CHECK(textOf(std::span{*wav}.subspan(36, 4)) == "data");
  CHECK(u32At(*wav, 40) == 8);
  CHECK(u16At(*wav, 44) == 1);
  CHECK(u16At(*wav, 46) == 0xFFFF);
  CHECK(u16At(*wav, 48) == 0x1234);
  CHECK(u16At(*wav, 50) == 0x8000);
  CHECK_FALSE(encodeWav(std::span{samples}.first(3), 2, 37800).has_value());
  CHECK_FALSE(encodeWav(samples, 0, 37800).has_value());
}

TEST_CASE("MOV_MOVIES gives each movie's path, first frame and frame count") {
  const Bytes overlay = movieOverlay();
  const auto image = MemoryImage::fromOverlay(overlay, MovieOverlayLoadAddress);
  const auto entries = readMovieTable(image, MovieOverlayLoadAddress + 0x100);
  REQUIRE_MESSAGE(entries.has_value(), errorOf(entries));
  REQUIRE(entries->size() == MovieCount);
  CHECK((*entries)[1].path == "\\MOVIE\\B.STR;1");
  CHECK((*entries)[1].startFrame == 1);
  CHECK((*entries)[3].frameCount == 1003);
  CHECK(movieVolumePath((*entries)[0].path) == "MOVIE/A.STR");
  CHECK(movieVolumePath((*entries)[2].path) == "C.STR");

  CHECK_FALSE(readMovieTable(image, MovieOverlayLoadAddress + 0x108).has_value());
  Bytes unterminated = overlay;
  for (std::size_t i = 0; i < 0x20; ++i) {
    fixtures::putU8(unterminated, 0x100 + i, 'X');
  }
  CHECK_FALSE(readMovieTable(MemoryImage::fromOverlay(unterminated, MovieOverlayLoadAddress),
                             MovieOverlayLoadAddress + 0x100)
                  .has_value());

  CHECK(movieTableAddress("us") == 0x8001094CU);
  CHECK_FALSE(movieTableAddress("xx").has_value());
}

TEST_CASE("a movie's info becomes a YAML document that matches its schema") {
  const auto document = movieToYaml(sampleInfo());
  CHECK(checkAgainstSchema(document, "movie").empty());
  CHECK(document.find("video")->find("fps")->find("numerator")->text() == "30");
  CHECK(document.find("playback")->find("frame_count")->text() == "9");

  MovieInfo bare = sampleInfo();
  bare.audio.reset();
  bare.tableIndex.reset();
  bare.tableEntry.reset();
  const auto bareDocument = movieToYaml(bare);
  CHECK(checkAgainstSchema(bareDocument, "movie").empty());
  CHECK(bareDocument.find("audio")->text() == "null");
  CHECK(bareDocument.find("playback")->text() == "null");
}
