#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "index_scale.hpp"

using dwe::assets::IndexedImage;
using dwe::assets::PaletteColor;
using dwe::dwx::indexScale;
using dwe::dwx::IndexScaleMode;
using dwe::dwx::supportsIndexScale;

namespace {

constexpr std::uint8_t A = 1;
constexpr std::uint8_t B = 2;

IndexedImage imageOf(std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t> indices) {
    return IndexedImage{
        .width = width,
        .height = height,
        .indices = std::move(indices),
        .palette = {PaletteColor{}, PaletteColor{.red = 255, .alpha = 255}, PaletteColor{.blue = 255, .alpha = 255}}};
}

// The checkerboard of two colours, the smallest image where the edge rules join the diagonals.
IndexedImage checkerboard() {
    return imageOf(2, 2, {A, B, B, A});
}

} // namespace

TEST_CASE("index_scale's edge rules double a checkerboard into joined diagonals") {
    const auto grown = indexScale(checkerboard(), 2, IndexScaleMode::Edge);
    REQUIRE(grown.has_value());
    CHECK(grown->width == 4);
    CHECK(grown->height == 4);
    // Worked out by hand from the 2x rules: each corner takes the colour of the two neighbours that meet there.
    CHECK(grown->indices == std::vector<std::uint8_t>{A, A, B, B, //
                                                      A, B, A, B, //
                                                      B, A, B, A, //
                                                      B, B, A, A});
    CHECK(grown->palette.size() == 3);
}

TEST_CASE("index_scale's edge rules triple a checkerboard's corner pixel along its diagonal") {
    const auto grown = indexScale(checkerboard(), 3, IndexScaleMode::Edge);
    REQUIRE(grown.has_value());
    REQUIRE(grown->width == 6);
    const auto cell = [&](std::uint32_t x, std::uint32_t y) { return grown->indices[(y * grown->width) + x]; };
    // The top left pixel: its corner towards the other A stays A, the cells towards the B diagonal turn B.
    CHECK(cell(0, 0) == A);
    CHECK(cell(1, 0) == A);
    CHECK(cell(2, 0) == A);
    CHECK(cell(0, 1) == A);
    CHECK(cell(1, 1) == A);
    CHECK(cell(2, 1) == B);
    CHECK(cell(0, 2) == A);
    CHECK(cell(1, 2) == B);
    CHECK(cell(2, 2) == B);
}

TEST_CASE("index_scale keeps flat areas and straight edges as they are") {
    // A one-pixel vertical line on a plain field: no corner of it has two agreeing neighbours across a diagonal.
    const IndexedImage line = imageOf(3, 3, {A, B, A, A, B, A, A, B, A});
    const auto grown = indexScale(line, 2, IndexScaleMode::Edge);
    REQUIRE(grown.has_value());
    const auto nearest = indexScale(line, 2, IndexScaleMode::Nearest);
    REQUIRE(nearest.has_value());
    CHECK(grown->indices == nearest->indices);
}

TEST_CASE("index_scale composes 4, 6 and 8 from the 2x and 3x rules, 2 first") {
    const auto four = indexScale(checkerboard(), 4, IndexScaleMode::Edge);
    const auto twice = indexScale(*indexScale(checkerboard(), 2, IndexScaleMode::Edge), 2, IndexScaleMode::Edge);
    REQUIRE(four.has_value());
    REQUIRE(twice.has_value());
    CHECK(four->indices == twice->indices);

    const auto six = indexScale(checkerboard(), 6, IndexScaleMode::Edge);
    const auto twoThenThree = indexScale(*indexScale(checkerboard(), 2, IndexScaleMode::Edge), 3, IndexScaleMode::Edge);
    REQUIRE(six.has_value());
    REQUIRE(twoThenThree.has_value());
    CHECK(six->indices == twoThenThree->indices);

    const auto eight = indexScale(checkerboard(), 8, IndexScaleMode::Edge);
    REQUIRE(eight.has_value());
    CHECK(eight->width == 16);
}

TEST_CASE("index_scale's nearest mode repeats each pixel") {
    const auto grown = indexScale(imageOf(2, 1, {A, B}), 3, IndexScaleMode::Nearest);
    REQUIRE(grown.has_value());
    CHECK(grown->indices == std::vector<std::uint8_t>{A, A, A, B, B, B, //
                                                      A, A, A, B, B, B, //
                                                      A, A, A, B, B, B});
}

TEST_CASE("index_scale refuses the factors its modes do not have, and malformed images") {
    CHECK(supportsIndexScale(IndexScaleMode::Edge, 6));
    CHECK_FALSE(supportsIndexScale(IndexScaleMode::Edge, 5));
    CHECK_FALSE(supportsIndexScale(IndexScaleMode::Edge, 7));
    CHECK(supportsIndexScale(IndexScaleMode::Nearest, 7));
    CHECK_FALSE(supportsIndexScale(IndexScaleMode::Nearest, 9));
    CHECK_FALSE(supportsIndexScale(IndexScaleMode::Nearest, 0));
    CHECK_FALSE(indexScale(checkerboard(), 5, IndexScaleMode::Edge).has_value());
    CHECK_FALSE(indexScale(imageOf(2, 2, {A, B, A}), 2, IndexScaleMode::Edge).has_value());
}
