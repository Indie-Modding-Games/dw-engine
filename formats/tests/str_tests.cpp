#include <dwe/formats/video/str.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "video_fixtures.hpp"

using namespace dwe::formats::video;
using dwe::formats::disc::SectorForm;
using Bytes = std::vector<std::byte>;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

constexpr std::uint8_t VideoSubmode = submode::Data | submode::RealTime;
constexpr std::uint8_t AudioSubmode = submode::Audio | submode::Form2 | submode::RealTime;

// A frame's bitstream of size bytes whose first 8 bytes are its headm and headv.
Bytes frameBytes(std::size_t size, std::uint8_t seed) {
  return fixtures::patternBytes(size, seed);
}

std::uint32_t wordAt(const Bytes& bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8 * i);
  }
  return value;
}

// The video sectors of one frame, split in chunks.
std::vector<Bytes> frameSectors(std::uint32_t number, const Bytes& frame, std::uint16_t sectors) {
  std::vector<Bytes> out;
  for (std::uint16_t i = 0; i < sectors; ++i) {
    const std::size_t begin = std::min(frame.size(), i * StChunkSize);
    const std::size_t end = std::min(frame.size(), begin + StChunkSize);
    const auto chunk = std::span{frame}.subspan(begin, end - begin);
    out.push_back(
        fixtures::makeVideoSectorData({.sectorInFrame = i,
                                       .sectorsInFrame = sectors,
                                       .frameNumber = number,
                                       .frameBytes = static_cast<std::uint32_t>(frame.size()),
                                       .headm = wordAt(frame, 0),
                                       .headv = wordAt(frame, 4)},
                                      chunk));
  }
  return out;
}

StrVideoSector videoSector(const Bytes& data) {
  const auto sector = readStrSector(fixtures::sectorOf(data, SectorForm::Form1, VideoSubmode));
  REQUIRE_MESSAGE(sector.has_value(), errorOf(sector));
  REQUIRE(std::holds_alternative<StrVideoSector>(*sector));
  return std::get<StrVideoSector>(*sector);
}

} // namespace

TEST_CASE("an 8-sector interleave gives its frames and its audio sector") {
  const Bytes first = frameBytes((3 * StChunkSize) + 100, 1);
  const Bytes second = frameBytes((2 * StChunkSize) + 7, 2);
  auto sectors = frameSectors(1, first, 4);
  for (auto& sector : frameSectors(2, second, 3)) {
    sectors.push_back(std::move(sector));
  }
  const Bytes audio(dwe::formats::disc::Form2DataSize);

  StrFrameAssembler assembler;
  std::vector<StrFrame> frames;
  std::size_t audioSectors = 0;
  for (std::size_t i = 0; i < 8; ++i) {
    const bool isAudio = i == 7;
    const auto data = isAudio ? fixtures::sectorOf(audio, SectorForm::Form2, AudioSubmode, 0x01)
                              : fixtures::sectorOf(sectors[i], SectorForm::Form1, VideoSubmode);
    const auto sector = readStrSector(data);
    REQUIRE_MESSAGE(sector.has_value(), errorOf(sector));
    if (const auto* audioSector = std::get_if<StrAudioSector>(&*sector)) {
      ++audioSectors;
      CHECK(audioSector->coding ==
            XaCoding{.channels = 2, .sampleRate = 37800, .bitsPerSample = 4});
      CHECK(audioSector->data.size() == dwe::formats::disc::Form2DataSize);
      CHECK_FALSE(audioSector->endOfFile);
      continue;
    }
    const auto& video = std::get<StrVideoSector>(*sector);
    const auto frame = assembler.add(video);
    REQUIRE_MESSAGE(frame.has_value(), errorOf(frame));
    CHECK(frame->has_value() == (i == 3 || i == 6));
    if (frame->has_value()) {
      frames.push_back(**frame);
    }
  }
  CHECK(audioSectors == 1);
  CHECK_FALSE(assembler.pendingFrame().has_value());
  REQUIRE(frames.size() == 2);
  CHECK(frames[0].number == 1);
  CHECK(frames[0].bitstream == first);
  CHECK(frames[1].number == 2);
  CHECK(frames[1].bitstream == second);
  CHECK(frames[1].width == 16);
}

TEST_CASE("empty sectors are padding, other untyped sectors are errors") {
  const Bytes zero(dwe::formats::disc::Form1DataSize);
  const auto padding = readStrSector(fixtures::sectorOf(zero, SectorForm::Form1, 0));
  REQUIRE(padding.has_value());
  CHECK(std::holds_alternative<StrPaddingSector>(*padding));
  CHECK(readStrSector(fixtures::sectorOf(zero, SectorForm::Form1, submode::EndOfFile)).has_value());

  Bytes data = zero;
  data[100] = std::byte{1};
  CHECK_FALSE(readStrSector(fixtures::sectorOf(data, SectorForm::Form1, 0)).has_value());
  CHECK_FALSE(
      readStrSector(fixtures::sectorOf(zero, SectorForm::Form1, submode::RealTime)).has_value());
}

TEST_CASE("malformed stream sectors are errors") {
  const Bytes frame = frameBytes(100, 3);
  const Bytes good = frameSectors(1, frame, 1)[0];
  REQUIRE(readStrSector(fixtures::sectorOf(good, SectorForm::Form1, VideoSubmode)).has_value());

  SUBCASE("no subheader") {
    const dwe::formats::disc::SectorData cooked{
        .form = SectorForm::Form1, .userData = good, .subheader = {}};
    CHECK_FALSE(readStrSector(cooked).has_value());
  }
  SUBCASE("wrong form") {
    CHECK_FALSE(
        readStrSector(fixtures::sectorOf(good, SectorForm::Form2, VideoSubmode)).has_value());
    CHECK_FALSE(
        readStrSector(fixtures::sectorOf(good, SectorForm::Form1, AudioSubmode, 0x01)).has_value());
  }
  SUBCASE("unknown audio coding") {
    const Bytes audio(dwe::formats::disc::Form2DataSize);
    CHECK_FALSE(readStrSector(fixtures::sectorOf(audio, SectorForm::Form2, AudioSubmode, 0x02))
                    .has_value());
  }
  SUBCASE("header fields") {
    Bytes data = good;
    fixtures::putU16(data, 0, 0x0161);
    CHECK_FALSE(
        readStrSector(fixtures::sectorOf(data, SectorForm::Form1, VideoSubmode)).has_value());
    data = good;
    fixtures::putU32(data, 0x1C, 1);
    CHECK_FALSE(
        readStrSector(fixtures::sectorOf(data, SectorForm::Form1, VideoSubmode)).has_value());
    data = good;
    fixtures::putU16(data, 4, 1);
    CHECK_FALSE(
        readStrSector(fixtures::sectorOf(data, SectorForm::Form1, VideoSubmode)).has_value());
    CHECK_FALSE(readStrSector(
                    fixtures::sectorOf(std::span{good}.first(100), SectorForm::Form1, VideoSubmode))
                    .has_value());
  }
}

TEST_CASE("the assembler rejects frames that do not hold together") {
  const Bytes frame = frameBytes(StChunkSize + 50, 4);
  const auto sectors = frameSectors(5, frame, 2);

  SUBCASE("a frame larger than its sectors") {
    Bytes data = sectors[0];
    fixtures::putU32(data, 0x0C, 3 * StChunkSize);
    StrFrameAssembler assembler;
    CHECK_FALSE(assembler.add(videoSector(data)).has_value());
  }
  SUBCASE("a missing first sector") {
    StrFrameAssembler assembler;
    CHECK_FALSE(assembler.add(videoSector(sectors[1])).has_value());
  }
  SUBCASE("a frame that starts before the previous one ends") {
    StrFrameAssembler assembler;
    REQUIRE(assembler.add(videoSector(sectors[0])).has_value());
    CHECK(assembler.pendingFrame() == 5);
    const auto next = frameSectors(6, frame, 2);
    CHECK_FALSE(assembler.add(videoSector(next[0])).has_value());
    CHECK(assembler.pendingFrame() == 6);
    const auto completed = assembler.add(videoSector(next[1]));
    REQUIRE_MESSAGE(completed.has_value(), errorOf(completed));
    CHECK((completed->has_value() && (*completed)->number == 6));
  }
  SUBCASE("sectors that disagree on the header") {
    Bytes data = sectors[1];
    fixtures::putU16(data, 0x10, 32);
    StrFrameAssembler assembler;
    REQUIRE(assembler.add(videoSector(sectors[0])).has_value());
    CHECK_FALSE(assembler.add(videoSector(data)).has_value());
    CHECK_FALSE(assembler.pendingFrame().has_value());
  }
  SUBCASE("data in the padding after the frame") {
    Bytes data = sectors[1];
    data[StHeaderSize + 60] = std::byte{9};
    StrFrameAssembler assembler;
    REQUIRE(assembler.add(videoSector(sectors[0])).has_value());
    CHECK_FALSE(assembler.add(videoSector(data)).has_value());
  }
  SUBCASE("headm and headv that are not the start of the bitstream") {
    Bytes first = sectors[0];
    Bytes second = sectors[1];
    fixtures::putU32(first, 0x14, 0);
    fixtures::putU32(second, 0x14, 0);
    StrFrameAssembler assembler;
    REQUIRE(assembler.add(videoSector(first)).has_value());
    CHECK_FALSE(assembler.add(videoSector(second)).has_value());
  }
}
