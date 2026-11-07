#include <doctest/doctest.h>

#include "arena_lighting.hpp"

namespace dwe::app {

TEST_CASE("std_setup.c STD_initializeLighting: the arena's three lights and its quarter ambient") {
  const render::ModelLighting lighting = arenaLighting();
  CHECK(lighting.ambient.x == doctest::Approx(0.25));
  CHECK(lighting.lights[0].color.x == doctest::Approx(255.0 / 256.0));
  CHECK(lighting.lights[1].color.y == doctest::Approx(160.0 / 256.0));
  CHECK(lighting.lights[2].color.z == doctest::Approx(96.0 / 256.0));
  // Where they travel, as the original's vectors in the engine's axes.
  CHECK(lighting.lights[0].direction == core::fromOriginalAxes({1000.0F, 1000.0F, 1000.0F}));
  CHECK(lighting.lights[1].direction == core::fromOriginalAxes({-1000.0F, 1000.0F, -1000.0F}));
  CHECK(lighting.lights[2].direction == core::fromOriginalAxes({-20.0F, 20.0F, 200.0F}));
}

} // namespace dwe::app
