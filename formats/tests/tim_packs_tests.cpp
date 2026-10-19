#include <dwe/formats/image/tim_packs.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "tim_builder.hpp"

using namespace dwe::formats::image;
using dwe::formats::image::test::appendTim;
using dwe::formats::image::test::bytesOf;
using dwe::formats::image::test::directTim;
using dwe::formats::image::test::indexedTim;
using dwe::formats::image::test::putU32;
using dwe::formats::image::test::TimSpec;

namespace {

TimSpec smallIndexedTim() {
    return indexedTim(TimPixelMode::Indexed4, {.x = 0, .y = 480, .width = 16, .height = 1},
                      std::vector<Color15>(16, 0x7FFF), {.x = 0, .y = 0, .width = 2, .height = 2},
                      bytesOf({1, 2, 3, 4, 5, 6, 7, 8}));
}

TimSpec smallDirectTim() {
    return directTim(TimPixelMode::Direct16, {.x = 0, .y = 0, .width = 2, .height = 1}, bytesOf({1, 0, 2, 0}));
}

} // namespace

TEST_CASE("a stacked TIM file is read up to the first word that is not a TIM magic") {
    std::vector<std::byte> file;
    appendTim(file, smallIndexedTim());
    const std::size_t second = file.size();
    appendTim(file, smallDirectTim());
    const std::size_t end = file.size();
    putU32(file, 0);
    appendTim(file, smallDirectTim()); // after the end marker: never read

    const auto stack = readStackedTims(file);
    REQUIRE(stack.has_value());
    REQUIRE(stack->entries.size() == 2);
    CHECK(stack->entries[0].offset == 0);
    CHECK(stack->entries[0].tim.mode == TimPixelMode::Indexed4);
    CHECK(stack->entries[1].offset == second);
    CHECK(stack->entries[1].tim.mode == TimPixelMode::Direct16);
    CHECK(stack->end == end);
    CHECK(stack->warnings.empty());
}

TEST_CASE("a stacked TIM file may end exactly after its last TIM") {
    std::vector<std::byte> file;
    appendTim(file, smallDirectTim());
    const auto stack = readStackedTims(file);
    REQUIRE(stack.has_value());
    CHECK(stack->entries.size() == 1);
    CHECK(stack->end == file.size());
}

TEST_CASE("stacked TIMs advance by width × height and warn when the block length disagrees") {
    // The first image block claims 8 more bytes than its 2 × 1 pixels; the original skips only what width × height
    // give, so the next TIM starts right after the pixels, inside the claimed length.
    auto longer = smallDirectTim();
    longer.imageLength = 12 + 4 + 8;
    std::vector<std::byte> file;
    appendTim(file, longer);
    const std::size_t second = file.size();
    appendTim(file, smallDirectTim());

    const auto stack = readStackedTims(file);
    REQUIRE(stack.has_value());
    REQUIRE(stack->entries.size() == 2);
    CHECK(stack->entries[1].offset == second);
    CHECK(stack->entries[0].tim.byteSize == second + 8);
    REQUIRE(stack->warnings.size() == 1);
    CHECK(stack->warnings[0].find("TIM 0") != std::string::npos);
}

TEST_CASE("a file that does not start with a TIM gives an empty stack and a warning") {
    const auto stack = readStackedTims(bytesOf({0x11, 0, 0, 0}));
    REQUIRE(stack.has_value());
    CHECK(stack->entries.empty());
    CHECK(stack->end == 0);
    CHECK(stack->warnings.size() == 1);
}

TEST_CASE("a truncated stacked TIM is an error") {
    std::vector<std::byte> file;
    appendTim(file, smallDirectTim());
    appendTim(file, smallIndexedTim());
    file.resize(file.size() - 1);
    CHECK_FALSE(readStackedTims(file).has_value());
}

TEST_CASE("a fixed-stride pack splits into equal entries") {
    constexpr std::size_t Stride = 64;
    std::vector<std::byte> pack(std::size_t{3} * Stride);
    for (std::size_t entry = 0; entry < 3; ++entry) {
        std::vector<std::byte> tim;
        appendTim(tim, smallDirectTim());
        std::ranges::copy(tim, pack.begin() + static_cast<std::ptrdiff_t>(entry * Stride));
    }
    const auto entries = splitPack(pack, Stride);
    REQUIRE(entries.has_value());
    REQUIRE(entries->size() == 3);
    for (const auto& entry : *entries) {
        CHECK(entry.size() == Stride);
        CHECK(readTim(entry).has_value());
    }
    CHECK((*entries)[2].data() == pack.data() + (2 * Stride));

    CHECK_FALSE(splitPack(pack, 0).has_value());
    CHECK_FALSE(splitPack(std::span{pack}.first(100), Stride).has_value());
    CHECK(splitPack({}, Stride)->empty());
}

TEST_CASE("known packs check their entry count") {
    const KnownPack pack{.path = "TEST\\PACK.BIN",
                         .strideBytes = 16,
                         .entryCount = 2,
                         .entryFormat = PackEntryFormat::Tim,
                         .indexedBy = "test id",
                         .source = "none"};
    const std::vector<std::byte> two(32);
    const std::vector<std::byte> three(48);
    CHECK(splitKnownPack(two, pack).has_value());
    CHECK_FALSE(splitKnownPack(three, pack).has_value());
}

TEST_CASE("the known pack table matches the sizes in the original's file table") {
    // The sizes of ALLTIM.TIM and CARD.ALL in FILE_OFFSET_TABLE (src/main/file_table_us.c), the same in every release.
    const auto sizeOf = [](std::string_view path) -> std::size_t {
        if (path == "CHDAT\\ALLTIM.TIM") {
            return 0x32A000;
        }
        if (path == "CARD\\CARD.ALL") {
            return 0x1CE000;
        }
        return 0;
    };
    REQUIRE(knownPacks().size() == 2);
    for (const auto& pack : knownPacks()) {
        CHECK(pack.strideBytes % 2048 == 0);
        CHECK(pack.entryFormat == PackEntryFormat::Tim);
        CHECK(pack.strideBytes * pack.entryCount == sizeOf(pack.path));
        CHECK_FALSE(pack.indexedBy.empty());
        CHECK_FALSE(pack.source.empty());
    }
}
