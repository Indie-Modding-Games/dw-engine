#include <dwe/game/fishing/effects.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace {

using namespace dwe::game::fishing;

// The frames a mark is drawn: FISH_tickMarkers runs after the roll in the same frame, then once a
// frame, and the drawing comes after both.
int drawnFrames(MarkerPool& pool) {
  int frames = 0;
  pool.tick();
  while (pool.count() > 0) {
    ++frames;
    pool.tick();
  }
  return frames;
}

// The sprites a ripple shows, one per frame, from the frame it appears to the one it ends.
std::vector<std::int32_t> rippleSteps(std::int32_t kind) {
  Ripple ripple{.kind = kind};
  std::vector<std::int32_t> steps;
  while (stepRipple(ripple)) {
    steps.push_back(ripple.sprite);
  }
  return steps;
}

} // namespace

TEST_CASE("fishing marks: a mark shows for 9 frames after its roll") {
  // Measured: lifetime 10 at FISH_spawnMarker, 9 after that frame, 8 one frame later; the rod's
  // tick (the roll) runs before FISH_tickMarkers in the same frame.
  MarkerPool pool;
  pool.spawn(true, glm::vec3{1868.0F, 0.0F, 210.0F});
  REQUIRE(pool.count() == 1);
  CHECK(pool.slots().front()->lifetime == 10);
  CHECK(drawnFrames(pool) == 9);
}

TEST_CASE("fishing marks: 16 at most, and a hooked fish clears them") {
  MarkerPool pool;
  for (int fish = 0; fish < 20; ++fish) {
    pool.spawn(fish % 2 == 0, glm::vec3{static_cast<float>(fish), 0.0F, 0.0F});
  }
  CHECK(pool.count() == EffectPoolSize);
  // The first 16 took the slots; the rest found none.
  CHECK(pool.slots().back()->position.x == 15.0F);
  pool.tick();
  CHECK(pool.count() == EffectPoolSize);
  pool.clear();
  CHECK(pool.count() == 0);
  // A freed slot is taken again from the last one used.
  pool.spawn(false, glm::vec3{0.0F});
  CHECK(pool.count() == 1);
}

TEST_CASE("fishing marks: a heart when the fish may bite, a cross when it may not") {
  const FishSprite heart = markerSprite({.mayBite = true});
  CHECK(heart.texel == glm::ivec2{0x68, 0x10});
  CHECK(heart.size == glm::ivec2{15, 15});
  CHECK(heart.origin == glm::ivec2{8, 16});
  CHECK(heart.palette == 4);
  const FishSprite cross = markerSprite({.mayBite = false});
  CHECK(cross.texel == glm::ivec2{0x58, 0});
  CHECK(cross.palette == 4);
}

TEST_CASE("fishing ripples: each kind steps through its table and ends") {
  // MAIN_D_80134958 at frame / 2: the splash lives 10 frames.
  CHECK(rippleSteps(0) == std::vector<std::int32_t>{1, 1, 2, 2, 3, 3, 4, 4, 5, 5});
  CHECK(rippleSteps(-1) == std::vector<std::int32_t>{0, 1, 2, 3, 4, 5});
  CHECK(rippleSteps(-2) == std::vector<std::int32_t>{4, 4, 4, 5, 5, 5});
  CHECK(rippleSteps(-3) == std::vector<std::int32_t>{2, 3, 4, 5});
  CHECK(rippleSteps(-4) ==
        std::vector<std::int32_t>{0, 0, 6, 7, 8, 9, 10, 11, 12, 2, 3, 4, 4, 5, 5, 5});
  CHECK(rippleSteps(-5).empty());
}

TEST_CASE("fishing ripples: the splash and the rings use their own rectangles") {
  CHECK_FALSE(rippleSprite({.kind = -1, .sprite = 0}));
  const std::optional<FishSprite> splash = rippleSprite({.kind = 0, .sprite = 3});
  REQUIRE(splash);
  CHECK(splash->texel == glm::ivec2{0xa0, 0x48});
  CHECK(splash->size == glm::ivec2{31, 15});
  CHECK(splash->origin == glm::ivec2{16, 8});
  const std::optional<FishSprite> ring = rippleSprite({.kind = -4, .sprite = 12});
  REQUIRE(ring);
  CHECK(ring->texel == glm::ivec2{0xe0, 0x38});
  CHECK(ring->palette == 0);
  // The lake's scale 0x3800 with a ring's 0x1000: 3.5 units a texel.
  CHECK(rippleUnitsPerTexel(0x3800, 0x1000) == doctest::Approx(3.5F));
}

TEST_CASE("fishing ripples: the scaring ripple scares while it lives") {
  RipplePool pool;
  REQUIRE(pool.spawn(0, glm::vec3{10.0F, 0.0F, 20.0F}, 100.0F, 0x1000));
  REQUIRE(pool.attractor() != nullptr);
  CHECK(pool.attractRadius() == 100.0F);
  // A ring that does not scare keeps the attractor.
  REQUIRE(pool.spawn(-3, glm::vec3{0.0F}, 0.0F, 0x1000));
  CHECK(pool.attractor()->kind == 0);
  int frames = 0;
  while (pool.attractor() != nullptr) {
    pool.tick();
    ++frames;
  }
  // Drawn 10 frames; the eleventh tick frees it.
  CHECK(frames == 11);
}

TEST_CASE("fishing ripples: no slot past 16") {
  RipplePool pool;
  for (int ripple = 0; ripple < 16; ++ripple) {
    CHECK(pool.spawn(-1, glm::vec3{0.0F}, 0.0F, 0x1000));
  }
  CHECK_FALSE(pool.spawn(0, glm::vec3{0.0F}, 700.0F, 0x1000));
  CHECK(pool.attractor() == nullptr);
}
