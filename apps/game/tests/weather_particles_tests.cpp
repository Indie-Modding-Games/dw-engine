#include <dwe/game/weather/look.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>

#include "weather_particles.hpp"

using dwe::app::WeatherFrame;
using dwe::app::WeatherQuad;
using dwe::app::WeatherShape;
using dwe::game::weather::lookOf;
using dwe::game::weather::WeatherKind;

namespace {

// A 1280 x 720 window: the map covers 960 x 720 at scale 3 (the original's 320 x 240).
WeatherFrame frameAt(double seconds, float daylight = 1.0F) {
  return {.areaOrigin = {160.0F, 0.0F},
          .areaSize = {960.0F, 720.0F},
          .scale = 3.0F,
          .scroll = {100.0F, 40.0F},
          .seconds = seconds,
          .daylight = daylight};
}

std::size_t countShape(const std::vector<WeatherQuad>& quads, WeatherShape shape) {
  return static_cast<std::size_t>(
      std::ranges::count_if(quads, [&](const WeatherQuad& quad) { return quad.shape == shape; }));
}

} // namespace

TEST_CASE("no weather draws nothing; a clear day only warms the picture") {
  CHECK(dwe::app::weatherQuads(lookOf(WeatherKind::None), frameAt(1.0)).empty());
  const auto sunny = dwe::app::weatherQuads(lookOf(WeatherKind::Clear), frameAt(1.0));
  REQUIRE(sunny.size() == 1);
  CHECK(sunny[0].blend == dwe::app::WeatherBlendMode::Additive);
  // At night the sun is down: nothing.
  CHECK(dwe::app::weatherQuads(lookOf(WeatherKind::Clear), frameAt(1.0, 0.0F)).empty());
}

TEST_CASE("rain falls as streaks and splashes inside the map's part of the window") {
  const auto heavy = dwe::app::weatherQuads(lookOf(WeatherKind::HeavyRain), frameAt(12.5));
  const std::size_t drops =
      countShape(heavy, WeatherShape::Streak) + countShape(heavy, WeatherShape::Ring);
  CHECK(drops == static_cast<std::size_t>(dwe::app::HeavyRainDrops));
  CHECK(countShape(heavy, WeatherShape::Streak) > countShape(heavy, WeatherShape::Ring));
  CHECK(countShape(heavy, WeatherShape::Ring) > 0);
  CHECK(countShape(heavy, WeatherShape::Clouds) == 1);
  // Splashes land inside the area and its margin of 24 original pixels (the drawer cuts the rest).
  for (const WeatherQuad& quad : heavy) {
    if (quad.shape != WeatherShape::Ring) {
      continue;
    }
    for (const glm::vec2 corner : quad.corners) {
      CHECK(corner.x >= 160.0F - 90.0F);
      CHECK(corner.x <= 1120.0F + 90.0F);
      CHECK(corner.y >= -90.0F);
      CHECK(corner.y <= 720.0F + 90.0F);
    }
  }
  // Light rain: fewer drops; the same moment always draws the same rain.
  const auto light = dwe::app::weatherQuads(lookOf(WeatherKind::LightRain), frameAt(12.5));
  CHECK(countShape(light, WeatherShape::Streak) < countShape(heavy, WeatherShape::Streak) / 2 + 5);
  const auto again = dwe::app::weatherQuads(lookOf(WeatherKind::HeavyRain), frameAt(12.5));
  REQUIRE(again.size() == heavy.size());
  CHECK(again.back().corners == heavy.back().corners);
  // It moves: a moment later the drops are elsewhere.
  const auto later = dwe::app::weatherQuads(lookOf(WeatherKind::HeavyRain), frameAt(12.55));
  CHECK(later.back().corners != heavy.back().corners);
}

TEST_CASE("snow drifts as soft flakes, dimmer at night") {
  const auto day = dwe::app::weatherQuads(lookOf(WeatherKind::HeavySnow), frameAt(3.0));
  CHECK(countShape(day, WeatherShape::Dot) == static_cast<std::size_t>(dwe::app::HeavySnowFlakes));
  CHECK(countShape(day, WeatherShape::Streak) == 0);
  const auto night = dwe::app::weatherQuads(lookOf(WeatherKind::HeavySnow), frameAt(3.0, 0.0F));
  const auto flake = [](const std::vector<WeatherQuad>& quads) {
    return *std::ranges::find_if(
        quads, [](const WeatherQuad& quad) { return quad.shape == WeatherShape::Dot; });
  };
  CHECK(flake(night).color.r < flake(day).color.r);
}

TEST_CASE("a half-faded rain draws about half its drops") {
  dwe::game::weather::WeatherBlend blend{8.0F};
  blend.snap(WeatherKind::Cloudy);
  blend.advance(WeatherKind::HeavyRain, 4.0);
  const auto quads = dwe::app::weatherQuads(blend.look(), frameAt(1.0));
  const std::size_t drops =
      countShape(quads, WeatherShape::Streak) + countShape(quads, WeatherShape::Ring);
  CHECK(drops == static_cast<std::size_t>(dwe::app::HeavyRainDrops / 2.0F));
}

TEST_CASE("the cloud picture tiles") {
  constexpr std::uint32_t Side = 64;
  const auto pixels = dwe::app::cloudNoisePixels(Side);
  REQUIRE(pixels.size() == Side * Side * 4);
  // The last column runs into the first: neighbours across the seam differ as little as inside.
  int seam = 0;
  for (std::uint32_t y = 0; y < Side; ++y) {
    const auto left = std::to_integer<int>(pixels[(y * Side) * 4]);
    const auto right = std::to_integer<int>(pixels[((y * Side) + Side - 1) * 4]);
    seam = std::max(seam, std::abs(left - right));
  }
  CHECK(seam < 40);
}
