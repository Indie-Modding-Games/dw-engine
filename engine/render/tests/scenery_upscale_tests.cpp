// HD scenery's upscaler and its cache (render::upscaleScenery, render::upscaleRgba,
// render::UpscaleCache; docs/architecture/remaster-pipeline.md, «Nitidez»): synthetic pictures
// with a known right answer.

#include <dwe/assets/indexed_image.hpp>
#include <dwe/render/scenery_upscale.hpp>
#include <dwe/render/upscale_cache.hpp>
#include <dwe/test_support/temp_folder.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

using namespace dwe;
using assets::PaletteColor;

namespace {

constexpr PaletteColor Clear{};

PaletteColor grey(std::uint8_t value) {
  return {.red = value, .green = value, .blue = value, .alpha = 255};
}

// A palette whose index i is grey i (index 0 included, opaque).
std::vector<PaletteColor> greys() {
  std::vector<PaletteColor> palette(256);
  for (std::size_t index = 0; index < palette.size(); ++index) {
    palette[index] = grey(static_cast<std::uint8_t>(index));
  }
  return palette;
}

// The indices of a width x height picture, index = pick(x, y).
template <typename Pick>
std::vector<std::uint8_t> picture(std::uint32_t width, std::uint32_t height, Pick pick) {
  std::vector<std::uint8_t> indices;
  indices.reserve(static_cast<std::size_t>(width) * height);
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      indices.push_back(static_cast<std::uint8_t>(pick(x, y)));
    }
  }
  return indices;
}

render::UpscaledScenery upscaled(std::uint32_t width, std::uint32_t height,
                                 std::span<const std::uint8_t> indices,
                                 std::vector<std::vector<PaletteColor>> palettes,
                                 render::UpscaleOptions options = {}) {
  return render::upscaleScenery(width, height, indices, palettes, options);
}

const PaletteColor& at(const render::UpscaledScenery& scenery, std::size_t variant, std::uint32_t u,
                       std::uint32_t v) {
  return scenery.variants[variant][(static_cast<std::size_t>(v) * scenery.width) + u];
}

constexpr render::UpscaleOptions Plain{.dither = 0.0F, .sharpen = 0.0F};

} // namespace

TEST_CASE("the upscaler doubles the picture and keeps each source texel at its even pixel") {
  constexpr std::uint32_t Width = 12;
  constexpr std::uint32_t Height = 9;
  // A busy picture: no two neighbours alike.
  const auto indices = picture(
      Width, Height, [](std::uint32_t x, std::uint32_t y) { return (x * 37 + y * 91) % 256; });
  const render::UpscaledScenery result = upscaled(Width, Height, indices, {greys()}, Plain);
  REQUIRE(result.width == Width * 2);
  REQUIRE(result.height == Height * 2);
  REQUIRE(result.variants.size() == 1);
  for (std::uint32_t y = 0; y < Height; ++y) {
    for (std::uint32_t x = 0; x < Width; ++x) {
      CAPTURE(x);
      CAPTURE(y);
      CHECK(at(result, 0, x * 2, y * 2).red == indices[(y * Width) + x]);
    }
  }
}

TEST_CASE("a flat area stays exactly flat, with the dithering melt and the sharpen on") {
  const auto indices = picture(16, 16, [](std::uint32_t, std::uint32_t) { return 120; });
  const render::UpscaledScenery result = upscaled(16, 16, indices, {greys()});
  for (const PaletteColor& colour : result.variants[0]) {
    CHECK(colour == grey(120));
  }
}

TEST_CASE("a hard edge comes out without ringing: no value beyond the two sides") {
  // A vertical step and a diagonal one: every new pixel stays within black and white.
  for (const bool diagonal : {false, true}) {
    const auto indices = picture(16, 16, [diagonal](std::uint32_t x, std::uint32_t y) {
      return (diagonal ? x > y : x >= 8) ? 200 : 40;
    });
    const render::UpscaledScenery result = upscaled(16, 16, indices, {greys()});
    for (const PaletteColor& colour : result.variants[0]) {
      CHECK(colour.red >= 40);
      CHECK(colour.red <= 200);
    }
  }
}

TEST_CASE("a diagonal edge comes out as a line, not stairs") {
  // White right of the diagonal x = y. Doubled pixel by pixel (nearest), the edge would move two
  // pixels every second row; along the edge it moves one pixel every row.
  constexpr std::uint32_t Side = 24;
  const auto indices =
      picture(Side, Side, [](std::uint32_t x, std::uint32_t y) { return x > y ? 255 : 0; });
  const render::UpscaledScenery result = upscaled(Side, Side, indices, {greys()}, Plain);
  std::vector<int> crossing;
  for (std::uint32_t v = 8; v < (Side * 2) - 8; ++v) {
    std::uint32_t u = 0;
    while (u < result.width && at(result, 0, u, v).red < 128) {
      ++u;
    }
    crossing.push_back(static_cast<int>(u));
  }
  for (std::size_t row = 1; row < crossing.size(); ++row) {
    CAPTURE(row);
    CHECK(crossing[row] - crossing[row - 1] == 1);
  }
}

TEST_CASE("a fade between palette variants upscales like the variants mixed afterwards") {
  // Two variants and their even mix: the mix's upscale is the mix of the upscales (to rounding),
  // so the GPU may mix the upscaled variants as the fade goes.
  constexpr std::uint32_t Side = 16;
  const auto indices = picture(Side, Side, [](std::uint32_t x, std::uint32_t y) {
    return ((x / 3) + (y / 2)) % 2 == 0 ? (x * 7 + y) % 64 : 128 + ((x + y * 5) % 64);
  });
  std::vector<PaletteColor> day(256);
  std::vector<PaletteColor> night(256);
  std::vector<PaletteColor> dusk(256);
  for (std::size_t index = 0; index < 256; ++index) {
    const auto value = static_cast<std::uint8_t>(index);
    day[index] = {
        .red = value, .green = static_cast<std::uint8_t>(255 - value), .blue = 90, .alpha = 255};
    night[index] = {.red = static_cast<std::uint8_t>(value / 4),
                    .green = 20,
                    .blue = static_cast<std::uint8_t>(value / 2),
                    .alpha = 255};
    dusk[index] = {.red = static_cast<std::uint8_t>((day[index].red + night[index].red) / 2),
                   .green = static_cast<std::uint8_t>((day[index].green + night[index].green) / 2),
                   .blue = static_cast<std::uint8_t>((day[index].blue + night[index].blue) / 2),
                   .alpha = 255};
  }
  const render::UpscaledScenery result = upscaled(Side, Side, indices, {day, night, dusk});
  REQUIRE(result.variants.size() == 3);
  // Only the clamp against ringing, per channel, and the rounding make a difference: a few levels
  // at most at a pixel, well under one on average.
  double largest = 0.0;
  double total = 0.0;
  for (std::size_t pixel = 0; pixel < result.variants[0].size(); ++pixel) {
    const PaletteColor& a = result.variants[0][pixel];
    const PaletteColor& b = result.variants[1][pixel];
    const PaletteColor& mix = result.variants[2][pixel];
    for (const double difference :
         {((a.red + b.red) / 2.0) - mix.red, ((a.green + b.green) / 2.0) - mix.green,
          ((a.blue + b.blue) / 2.0) - mix.blue}) {
      largest = std::max(largest, std::abs(difference));
      total += std::abs(difference);
    }
  }
  CHECK(largest <= 8.0);
  CHECK(total / static_cast<double>(result.variants[0].size() * 3) < 1.0);
}

TEST_CASE("transparent and STP texels never mix with opaque ones, and keep their class") {
  // Left half opaque white, a middle column of STP grey, right half never drawn (colour 0, black).
  constexpr std::uint32_t Side = 12;
  std::vector<PaletteColor> palette(256, grey(255));
  palette[0] = Clear;
  palette[1] = {.red = 100, .green = 100, .blue = 100, .alpha = 128};
  const auto indices = picture(
      Side, Side, [](std::uint32_t x, std::uint32_t) { return x < 5 ? 2 : (x == 5 ? 1 : 0); });
  const render::UpscaledScenery result = upscaled(Side, Side, indices, {palette});
  for (std::uint32_t v = 0; v < result.height; ++v) {
    for (std::uint32_t u = 0; u < result.width; ++u) {
      const PaletteColor& colour = at(result, 0, u, v);
      const std::uint32_t source = u / 2;
      CAPTURE(u);
      CAPTURE(v);
      if (source < 5) {
        CHECK(colour == grey(255));
      } else if (source == 5) {
        CHECK(colour == palette[1]);
      } else {
        CHECK(colour.alpha == 0);
      }
    }
  }
}

TEST_CASE("low-contrast dithering melts into its shade, contrasted detail does not") {
  // A checkerboard of two close greys becomes their mean; one of black and white stays.
  const auto close = picture(
      16, 16, [](std::uint32_t x, std::uint32_t y) { return (x + y) % 2 == 0 ? 100 : 106; });
  const render::UpscaledScenery melted = upscaled(16, 16, close, {greys()});
  for (std::uint32_t v = 4; v < 28; ++v) {
    for (std::uint32_t u = 4; u < 28; ++u) {
      CHECK(std::abs(static_cast<int>(at(melted, 0, u, v).red) - 103) <= 1);
    }
  }
  const auto contrasted =
      picture(16, 16, [](std::uint32_t x, std::uint32_t y) { return (x + y) % 2 == 0 ? 0 : 255; });
  const render::UpscaledScenery kept = upscaled(16, 16, contrasted, {greys()});
  CHECK(at(kept, 0, 16, 16).red == 0);
  CHECK(at(kept, 0, 18, 16).red == 255);
}

TEST_CASE("the sharpen adds definition to a soft step and stays within its sides") {
  // A ramp from 60 to 180 over a few texels: sharpened, its slope is steeper in the middle.
  const auto indices = picture(16, 4, [](std::uint32_t x, std::uint32_t) {
    return std::clamp<int>(60 + (static_cast<int>(x) - 6) * 30, 60, 180);
  });
  const render::UpscaledScenery soft = upscaled(16, 4, indices, {greys()}, Plain);
  const render::UpscaledScenery sharp =
      upscaled(16, 4, indices, {greys()}, {.dither = 0.0F, .sharpen = 1.0F});
  int softSlope = 0;
  int sharpSlope = 0;
  for (std::uint32_t u = 1; u < soft.width; ++u) {
    softSlope = std::max(softSlope, at(soft, 0, u, 3).red - at(soft, 0, u - 1, 3).red);
    sharpSlope = std::max(sharpSlope, at(sharp, 0, u, 3).red - at(sharp, 0, u - 1, 3).red);
    CHECK(at(sharp, 0, u, 3).red >= 50);
    CHECK(at(sharp, 0, u, 3).red <= 190);
  }
  CHECK(sharpSlope > softSlope);
}

TEST_CASE("a model texture's cut-out edge takes no dark fringe from its transparent texels") {
  // A white square in the middle of transparent black texels.
  constexpr std::uint32_t Side = 8;
  std::vector<PaletteColor> texels(Side * Side, Clear);
  for (std::uint32_t y = 2; y < 6; ++y) {
    for (std::uint32_t x = 2; x < 6; ++x) {
      texels[(y * Side) + x] = grey(250);
    }
  }
  const std::vector<std::byte> doubled =
      render::upscaleRgba(Side, Side, std::as_bytes(std::span{texels}), 1);
  REQUIRE(doubled.size() == static_cast<std::size_t>(Side) * 2 * Side * 2 * 4);
  bool soft = false;
  for (std::size_t pixel = 0; pixel < doubled.size() / 4; ++pixel) {
    const auto alpha = static_cast<std::uint8_t>(doubled[(pixel * 4) + 3]);
    if (alpha > 0) {
      CHECK(static_cast<std::uint8_t>(doubled[pixel * 4]) >= 240);
    }
    soft = soft || (alpha > 0 && alpha < 255);
  }
  // The outline is interpolated: some pixels are part covered.
  CHECK(soft);
  // No doubling: the same size, transparent texels coloured by their neighbours.
  const std::vector<std::byte> same =
      render::upscaleRgba(Side, Side, std::as_bytes(std::span{texels}), 0);
  REQUIRE(same.size() == texels.size() * 4);
  CHECK(static_cast<std::uint8_t>(same[((1 * Side) + 2) * 4]) == 250);
  CHECK(static_cast<std::uint8_t>(same[(((1 * Side) + 2) * 4) + 3]) == 0);
}

TEST_CASE("the upscale cache keeps a result under its key and drops what does not fit") {
  const test_support::TempFolder folder;
  const render::UpscaleCache cache{folder.path() / "cache"};
  std::vector<std::byte> data(1000);
  for (std::size_t index = 0; index < data.size(); ++index) {
    data[index] = static_cast<std::byte>((index * 7) % 251);
  }
  const std::uint64_t key = render::UpscaleKey{}.add(std::uint32_t{1}).add(data).value();
  CHECK_FALSE(cache.load(key, data.size()));
  cache.store(key, data);
  const auto kept = cache.load(key, data.size());
  REQUIRE(kept);
  CHECK(*kept == data);
  // Another size is another result: not this one.
  CHECK_FALSE(cache.load(key, data.size() + 1));
  // A damaged file is ignored.
  for (const auto& entry : std::filesystem::directory_iterator{folder.path() / "cache"}) {
    std::ofstream{entry.path(), std::ios::binary | std::ios::trunc} << "DWU1 broken";
  }
  CHECK_FALSE(cache.load(key, data.size()));
  // Without a folder nothing is kept.
  const render::UpscaleCache none;
  none.store(key, data);
  CHECK_FALSE(none.load(key, data.size()));
}

TEST_CASE("the upscale cache stays under its limit, dropping what was used longest ago") {
  const test_support::TempFolder folder;
  // Room for about two of these (incompressible bytes: each file is a little over 4 KiB).
  const render::UpscaleCache cache{folder.path(), 9000};
  const auto entry = [](std::uint32_t seed) {
    std::vector<std::byte> data(4096);
    std::uint32_t state = seed;
    for (std::byte& value : data) {
      state = (state * 1664525U) + 1013904223U;
      value = static_cast<std::byte>(state >> 24U);
    }
    return data;
  };
  cache.store(1, entry(1));
  cache.store(2, entry(2));
  // The first is used again, so the second is the oldest when the third comes.
  REQUIRE(cache.load(1, 4096));
  std::filesystem::last_write_time(folder.path() / "0000000000000002.bin",
                                   std::filesystem::file_time_type::clock::now() -
                                       std::chrono::hours{1});
  cache.store(3, entry(3));
  CHECK(cache.load(1, 4096));
  CHECK_FALSE(cache.load(2, 4096));
  CHECK(cache.load(3, 4096));
}

TEST_CASE("the upscale key changes with any byte of what it hashes") {
  const std::vector<std::byte> a(64, std::byte{3});
  std::vector<std::byte> b = a;
  b[40] = std::byte{4};
  CHECK(render::UpscaleKey{}.add(a).value() == render::UpscaleKey{}.add(a).value());
  CHECK(render::UpscaleKey{}.add(a).value() != render::UpscaleKey{}.add(b).value());
  CHECK(render::UpscaleKey{}.add(1U).add(a).value() != render::UpscaleKey{}.add(2U).add(a).value());
}
