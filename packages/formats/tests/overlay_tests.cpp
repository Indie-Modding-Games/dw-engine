#include <dwe/formats/exe/overlay.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "overlay_fixture.hpp"

using dwe::formats::exe::findOverlayLayout;
using dwe::formats::exe::OverlayDataKind;
using dwe::formats::exe::OverlayDataRange;
using dwe::formats::exe::OverlayEmbeddedFile;
using dwe::formats::exe::OverlayHeaderSize;
using dwe::formats::exe::OverlayLayout;
using dwe::formats::exe::overlayLayouts;
using dwe::formats::exe::readOverlayHeader;
using dwe::formats::exe::readOverlayParts;
using dwe::formats::test::makeOverlayFile;
using dwe::formats::test::OverlayFileSpec;
using dwe::formats::test::putU32;

namespace {

constexpr std::uint32_t Load = 0x80080000;

OverlayLayout testLayout(std::size_t headerOffset, std::optional<std::size_t> storedMemoryOffset) {
  return {.version = "test",
          .path = "TEST_REL.BIN",
          .loadAddress = Load,
          .headerOffset = headerOffset,
          .embeddedFiles = {},
          .storedMemoryOffset = storedMemoryOffset,
          .build = "test",
          .note = {},
          .dataRanges = {}};
}

// A data map of 0x20 bytes after 0x20 bytes of code: a jump table, pointers, text and numbers.
constexpr std::array TestDataRanges = {
    OverlayDataRange{.begin = 0x60,
                     .end = 0x68,
                     .kind = OverlayDataKind::JumpTable,
                     .what = "a switch",
                     .executableAddress = 0},
    OverlayDataRange{.begin = 0x68,
                     .end = 0x70,
                     .kind = OverlayDataKind::Pointers,
                     .what = "a table",
                     .executableAddress = 0},
    OverlayDataRange{.begin = 0x70,
                     .end = 0x78,
                     .kind = OverlayDataKind::Text,
                     .what = "a text",
                     .executableAddress = 0},
    OverlayDataRange{.begin = 0x78,
                     .end = 0x80,
                     .kind = OverlayDataKind::Numbers,
                     .what = "numbers",
                     .executableAddress = 0},
};

// The overlay the test data map describes: cases into the code, a zero and a pointer into the data,
// a text.
std::vector<std::byte> mappedOverlay() {
  auto file = makeOverlayFile({.leading = {},
                               .id = 1,
                               .address = Load,
                               .name = "TEST_REL.BIN",
                               .codeSize = 0x20,
                               .data = std::vector<std::byte>(0x20, std::byte{7})});
  putU32(file, 0x60, Load + 0x40);
  putU32(file, 0x64, Load + 0x5C);
  putU32(file, 0x68, 0);
  putU32(file, 0x6C, Load + 0x78);
  file[0x77] = std::byte{0};
  return file;
}

OverlayLayout mappedLayout(std::span<const OverlayDataRange> ranges) {
  OverlayLayout layout = testLayout(0, std::nullopt);
  layout.dataRanges = ranges;
  return layout;
}

} // namespace

TEST_CASE("an overlay header gives its id, address, sizes and name") {
  const auto file = makeOverlayFile({.leading = std::vector<std::byte>(8),
                                     .id = 7,
                                     .address = Load + 8,
                                     .name = "ABC_REL.BIN",
                                     .codeSize = 0x10,
                                     .data = std::vector<std::byte>(12)});
  const auto header = readOverlayHeader(file, 8);
  REQUIRE(header.has_value());
  CHECK(header->id == 7);
  CHECK(header->address == Load + 8);
  CHECK(header->codeSize == 0x10);
  CHECK(header->dataSize == 12);
  CHECK(header->name == "ABC_REL.BIN");
  CHECK_FALSE(readOverlayHeader(file, 0).has_value());
  CHECK_FALSE(readOverlayHeader(file, file.size() - 8).has_value());
}

TEST_CASE(
    "a header with a byte set where every known one has zero, or a name without its NUL, is refused") {
  const auto good = makeOverlayFile({});
  for (const std::size_t offset :
       {std::size_t{5}, std::size_t{0x14}, std::size_t{0x1F}, std::size_t{0x3F}}) {
    auto bad = good;
    bad[offset] = std::byte{1};
    CHECK_FALSE(readOverlayHeader(bad, 0).has_value());
  }
  auto unterminated = good;
  for (std::size_t i = 0x20; i < OverlayHeaderSize; ++i) {
    unterminated[i] = std::byte{'A'};
  }
  CHECK_FALSE(readOverlayHeader(unterminated, 0).has_value());
}

TEST_CASE("an overlay is cut into its files, header, code, data, stored memory and trailer") {
  constexpr std::size_t Leading = 0x10;
  const std::array embedded = {
      OverlayEmbeddedFile{.offset = 0, .size = Leading, .discPath = "DATA/A.BIN"}};
  OverlayLayout layout = testLayout(Leading, Leading + OverlayHeaderSize + 0x20 + 8);
  layout.embeddedFiles = embedded;
  const auto file = makeOverlayFile({.leading = std::vector<std::byte>(Leading, std::byte{9}),
                                     .id = 1,
                                     .address = Load + Leading,
                                     .name = "TEST_REL.BIN",
                                     .codeSize = 0x20,
                                     .data = std::vector<std::byte>(0x18)});
  const auto parts = readOverlayParts(file, layout);
  REQUIRE(parts.has_value());
  CHECK(parts->mismatches.empty());
  CHECK(parts->embedded.begin == 0);
  CHECK(parts->embedded.end == Leading);
  CHECK(parts->code.begin == Leading + OverlayHeaderSize);
  CHECK(parts->code.size() == 0x20);
  CHECK(parts->data.begin == parts->code.end);
  CHECK(parts->data.size() == 8);
  CHECK(parts->storedMemory.begin == parts->data.end);
  CHECK(parts->storedMemory.size() == 0x10);
  CHECK(parts->trailer.begin == file.size() - 4);
  CHECK(parts->trailer.end == file.size());

  // Without stored memory the data runs to the trailer.
  const auto plain = readOverlayParts(file, testLayout(Leading, std::nullopt));
  REQUIRE(plain.has_value());
  CHECK(plain->data.size() == 0x18);
  CHECK(plain->storedMemory.size() == 0);
}

TEST_CASE("where a file does not match its layout, the parts say so") {
  const auto file = makeOverlayFile({.leading = {},
                                     .id = 1,
                                     .address = Load,
                                     .name = "TEST_REL.BIN",
                                     .codeSize = 0x20,
                                     .data = std::vector<std::byte>(0x10)});
  const auto layout = testLayout(0, std::nullopt);

  auto movedHeader = file;
  movedHeader[8] = std::byte{4};
  const auto moved = readOverlayParts(movedHeader, layout);
  REQUIRE(moved.has_value());
  CHECK(moved->mismatches.size() == 1);

  auto longer = file;
  longer.resize(file.size() + 4);
  const auto sizes = readOverlayParts(longer, layout);
  REQUIRE(sizes.has_value());
  CHECK(sizes->mismatches.size() == 1);
  CHECK(sizes->data.end == longer.size());

  auto trailer = file;
  trailer.back() = std::byte{1};
  const auto ended = readOverlayParts(trailer, layout);
  REQUIRE(ended.has_value());
  CHECK(ended->mismatches.size() == 1);

  const auto outside = readOverlayParts(file, testLayout(0, 0x10));
  REQUIRE(outside.has_value());
  CHECK(outside->mismatches.size() == 1);
  CHECK(outside->storedMemory.size() == 0);

  CHECK_FALSE(readOverlayParts(file, testLayout(4, std::nullopt)).has_value());
}

TEST_CASE("an overlay's data map is checked range by range") {
  const auto file = mappedOverlay();
  const auto layout = mappedLayout(TestDataRanges);
  const auto parts = readOverlayParts(file, layout);
  REQUIRE(parts.has_value());
  CHECK(parts->mismatches.empty());
  CHECK(parts->dataRanges.size() == TestDataRanges.size());

  const auto mismatchOf = [&](std::size_t offset, std::uint32_t word) {
    auto changed = file;
    putU32(changed, offset, word);
    const auto checked = readOverlayParts(changed, layout);
    REQUIRE(checked.has_value());
    CHECK(checked->dataRanges.size() == TestDataRanges.size() - 1);
    REQUIRE(checked->mismatches.size() == 1);
    return checked->mismatches[0];
  };
  CHECK(mismatchOf(0x64, Load + 0x60).starts_with("[0x60, 0x68): a switch: the word at 0x64"));
  CHECK(mismatchOf(0x60, Load + 0x42).contains("does not point into the code"));
  CHECK(mismatchOf(0x60, 0).contains("does not point into the code"));
  CHECK(mismatchOf(0x6C, Load + 0x40).starts_with("[0x68, 0x70): a table"));
  CHECK(mismatchOf(0x74, 0x07070707)
            .starts_with("[0x70, 0x78): a text: the text does not end with a zero"));
}

TEST_CASE("a data map that leaves a gap in the data, or runs past it, is a mismatch") {
  const auto file = mappedOverlay();
  const auto shorter = readOverlayParts(file, mappedLayout(std::span{TestDataRanges}.first(3)));
  REQUIRE(shorter.has_value());
  REQUIRE(shorter->mismatches.size() == 1);
  CHECK(shorter->mismatches[0] == "[0x78, 0x80): the data map stops before the end of the data");

  const auto gap = readOverlayParts(file, mappedLayout(std::span{TestDataRanges}.subspan(1)));
  REQUIRE(gap.has_value());
  REQUIRE(gap->mismatches.size() == 1);
  CHECK(gap->mismatches[0].starts_with(
      "[0x60, 0x80): the data map does not cover the data end to end"));
  CHECK(gap->dataRanges.empty());

  // A header whose data ends 8 bytes earlier: the last range runs past the data.
  auto shortened = file;
  shortened.resize(shortened.size() - 8);
  putU32(shortened, 0x10, 0x18);
  putU32(shortened, 0x6C, 0);
  putU32(shortened, shortened.size() - 4, 0);
  const auto past = readOverlayParts(shortened, mappedLayout(TestDataRanges));
  REQUIRE(past.has_value());
  REQUIRE(past->mismatches.size() == 1);
  CHECK(past->mismatches[0].ends_with("its range [0x78, 0x80) does not come next"));
  CHECK(past->dataRanges.size() == 3);
}

TEST_CASE("the US layouts cover the sixteen overlays") {
  std::size_t us = 0;
  for (const OverlayLayout& layout : overlayLayouts()) {
    if (layout.version != "us") {
      continue;
    }
    ++us;
    CHECK(layout.path.ends_with("_REL.BIN"));
    std::size_t next = 0;
    for (const auto& embedded : layout.embeddedFiles) {
      CHECK(embedded.offset == next);
      next = embedded.offset + embedded.size;
    }
    CHECK(next == layout.headerOffset);
    for (std::size_t i = 1; i < layout.dataRanges.size(); ++i) {
      CHECK(layout.dataRanges[i].begin == layout.dataRanges[i - 1].end);
    }
    CHECK((layout.build == "us" || !layout.dataRanges.empty()));
  }
  CHECK(us == 16);
  REQUIRE(findOverlayLayout("us", "FISH_REL.BIN").has_value());
  REQUIRE(findOverlayLayout("us", "SHOP_REL.BIN").has_value());
  REQUIRE(findOverlayLayout("us", "STD_REL.BIN").has_value());
  const OverlayLayout fishing = findOverlayLayout("us", "FISH_REL.BIN").value_or(OverlayLayout{});
  CHECK(fishing.loadAddress == 0x80070000);
  CHECK(fishing.build == "us");
  CHECK(fishing.dataRanges.empty());
  const OverlayLayout shop = findOverlayLayout("us", "SHOP_REL.BIN").value_or(OverlayLayout{});
  CHECK(shop.build == "jp_rev1");
  CHECK_FALSE(shop.note.empty());
  REQUIRE_FALSE(shop.dataRanges.empty());
  CHECK(shop.dataRanges.front().begin == 0x7860);
  CHECK(shop.dataRanges.back().end == 0x7E0C);
  CHECK(findOverlayLayout("us", "STD_REL.BIN").value_or(OverlayLayout{}).embeddedFiles.size() == 5);
  CHECK_FALSE(findOverlayLayout("other", "FISH_REL.BIN").has_value());
  CHECK_FALSE(findOverlayLayout("us", "NONE_REL.BIN").has_value());
}
