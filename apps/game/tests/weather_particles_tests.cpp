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

TEST_CASE("the seasons drop a few petals in spring and leaves in autumn, none when neutral") {
  using dwe::game::weather::Season;
  using dwe::game::weather::SeasonLook;
  const auto clear = lookOf(WeatherKind::Clear);
  CHECK(dwe::app::seasonQuads(SeasonLook{}, clear, frameAt(3.0)).empty());
  const SeasonLook spring{.season = Season::Spring, .grade = {}, .petals = 1.0F};
  const SeasonLook autumn{.season = Season::Autumn, .grade = {}, .petals = 0.0F, .leaves = 1.0F};
  const auto petals = dwe::app::seasonQuads(spring, clear, frameAt(3.0));
  const auto leaves = dwe::app::seasonQuads(autumn, clear, frameAt(3.0));
  CHECK(countShape(petals, WeatherShape::Petal) ==
        static_cast<std::size_t>(dwe::app::SeasonPetals));
  CHECK(countShape(petals, WeatherShape::Leaf) == 0);
  CHECK(countShape(leaves, WeatherShape::Leaf) == static_cast<std::size_t>(dwe::app::SeasonLeaves));
  // Light: far fewer than the snow's flakes, and they move as time goes on.
  CHECK(leaves.size() < static_cast<std::size_t>(dwe::app::HeavySnowFlakes) / 5);
  const auto later = dwe::app::seasonQuads(autumn, clear, frameAt(4.0));
  CHECK(later.size() == leaves.size());
  CHECK(later[0].corners[0] != leaves[0].corners[0]);
  CHECK(dwe::app::seasonQuads(autumn, clear, frameAt(3.0))[0].corners == leaves[0].corners);
  // Fewer under heavy rain, dimmer at night.
  const auto rainy = dwe::app::seasonQuads(autumn, lookOf(WeatherKind::HeavyRain), frameAt(3.0));
  CHECK(rainy.size() < leaves.size() / 2 + 1);
  CHECK(!rainy.empty());
  const auto night = dwe::app::seasonQuads(autumn, clear, frameAt(3.0, 0.0F));
  CHECK(night[0].color.r < leaves[0].color.r);
}

TEST_CASE("fog draws one haze, thicker at the top of the map, and draws nothing else") {
  const auto fog = dwe::app::weatherQuads(lookOf(WeatherKind::Fog), frameAt(3.0));
  REQUIRE(countShape(fog, WeatherShape::Fog) == 1);
  const auto haze = std::ranges::find(fog, WeatherShape::Fog, &WeatherQuad::shape);
  // Corners 0 and 1 are the top: farther, thicker.
  CHECK(haze->cornerAlpha[0] > haze->cornerAlpha[2]);
  CHECK(haze->cornerAlpha[1] > haze->cornerAlpha[3]);
  CHECK(countShape(fog, WeatherShape::Streak) == 0);
  CHECK(countShape(fog, WeatherShape::Dot) == 0);
  // Its wisps drift.
  const auto later = dwe::app::weatherQuads(lookOf(WeatherKind::Fog), frameAt(13.0));
  CHECK(std::ranges::find(later, WeatherShape::Fog, &WeatherQuad::shape)->uv != haze->uv);
}

TEST_CASE("wind blows leaves, or dust in the dry areas, and slants the rain") {
  const auto leaves = dwe::app::weatherQuads(lookOf(WeatherKind::Wind), frameAt(5.0));
  CHECK(countShape(leaves, WeatherShape::Leaf) == static_cast<std::size_t>(dwe::app::WindLeaves));
  CHECK(countShape(leaves, WeatherShape::Dot) == 0);
  WeatherFrame dry = frameAt(5.0);
  dry.dust = true;
  const auto dust = dwe::app::weatherQuads(lookOf(WeatherKind::Wind), dry);
  CHECK(countShape(dust, WeatherShape::Dot) == static_cast<std::size_t>(dwe::app::WindDust));
  CHECK(countShape(dust, WeatherShape::Leaf) == 0);
  // They move sideways: a moment later the first is to the right (or wrapped around).
  const auto later = dwe::app::weatherQuads(lookOf(WeatherKind::Wind), frameAt(5.05));
  const auto first = std::ranges::find(leaves, WeatherShape::Leaf, &WeatherQuad::shape);
  const auto moved = std::ranges::find(later, WeatherShape::Leaf, &WeatherQuad::shape);
  CHECK(moved->corners[0].x != first->corners[0].x);
  // A storm's rain leans more than plain heavy rain.
  const auto slant = [](const std::vector<WeatherQuad>& quads) {
    const auto streak = std::ranges::find(quads, WeatherShape::Streak, &WeatherQuad::shape);
    const glm::vec2 along = streak->corners[2] - streak->corners[0];
    return along.x / along.y;
  };
  const auto storm = dwe::app::weatherQuads(lookOf(WeatherKind::Storm), frameAt(12.5));
  const auto heavy = dwe::app::weatherQuads(lookOf(WeatherKind::HeavyRain), frameAt(12.5));
  CHECK(slant(storm) > slant(heavy) + 0.2F);
}

TEST_CASE("a storm's lightning flashes now and then, the same every time, and brightens the map") {
  int flashing = 0;
  int strikes = 0;
  for (int step = 0; step < 6000; ++step) {
    const double seconds = step * 0.02;
    const float flash = dwe::app::lightningFlash(seconds);
    CHECK(flash >= 0.0F);
    CHECK(flash <= 1.0F);
    CHECK(flash == dwe::app::lightningFlash(seconds));
    flashing += flash > 0.05F ? 1 : 0;
  }
  for (std::int64_t slot = 0; slot < 100; ++slot) {
    if (const auto strike = dwe::app::lightningStrike(slot)) {
      ++strikes;
      // Within its slot, and the thunder after the flash.
      CHECK(strike->at >= static_cast<double>(slot) * dwe::app::LightningSlotSeconds);
      CHECK(strike->at < static_cast<double>(slot + 1) * dwe::app::LightningSlotSeconds);
      CHECK(strike->thunder > strike->at);
      CHECK(strike->thunder <= strike->at + dwe::app::LongestThunderDelay);
      CHECK(dwe::app::lightningFlash(strike->at + 0.15) > 0.3F);
    }
  }
  // About 7 strikes in 10 slots, each flashing for a fraction of a second.
  CHECK(strikes > 50);
  CHECK(strikes < 90);
  CHECK(flashing > 20);
  CHECK(flashing < 600);
  // At a flash, the storm adds a bright additive quad over the map; after the flash fades (within
  // its slot) it does not.
  std::int64_t slot = 0;
  while (!dwe::app::lightningStrike(slot)) {
    ++slot;
  }
  const double at = dwe::app::lightningStrike(slot)->at;
  const auto additive = [](const std::vector<WeatherQuad>& quads) {
    return std::ranges::count_if(quads, [](const WeatherQuad& quad) {
      return quad.blend == dwe::app::WeatherBlendMode::Additive;
    });
  };
  CHECK(additive(dwe::app::weatherQuads(lookOf(WeatherKind::Storm), frameAt(at + 0.15))) == 1);
  CHECK(additive(dwe::app::weatherQuads(lookOf(WeatherKind::Storm), frameAt(at + 0.9))) == 0);
}
