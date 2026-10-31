#include <dwe/core/original_math.hpp>

#include <doctest/doctest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "field_effects.hpp"

using dwe::app::FieldEffects;
using dwe::app::FieldEffectView;
using dwe::app::ObjectSmoke;
using dwe::app::ScenePrim;
using dwe::app::ScenePrimBlend;

namespace {

constexpr std::uint64_t Seed = 7;
constexpr float Focal = 1000.0F;
// The original's points 2000 in front of a camera at the origin looking down its +z (the world's
// -z).
constexpr std::int32_t Ahead = 2000;

FieldEffectView viewOf() {
  FieldEffectView view;
  view.frame = {.view = glm::mat4{1.0F},
                .projection = glm::perspective(glm::radians(60.0F), 4.0F / 3.0F, 10.0F, 100000.0F),
                .pixelScale = 1.0F};
  view.focalLength = Focal;
  view.images = {.clouds = dwe::rhi::TextureId{1},
                 .particles = dwe::rhi::TextureId{2},
                 .butterfly = dwe::rhi::TextureId{3},
                 .glow = dwe::rhi::TextureId{4}};
  return view;
}

std::size_t countTexture(const std::vector<ScenePrim>& prims, std::uint32_t texture) {
  std::size_t count = 0;
  for (const ScenePrim& prim : prims) {
    count += prim.texture.index == texture ? 1 : 0;
  }
  return count;
}

} // namespace

TEST_CASE("field effects: a cloud rises and fades over 14 frames, its size from its table") {
  // efe.c: tickCloudFX ends a cloud when its state reaches 0xE; renderCloudFX takes the height,
  // size, column and shade of the state.
  FieldEffects effects{Seed};
  effects.addCloud(0, Ahead);
  const FieldEffectView view = viewOf();
  std::vector<ScenePrim> prims = effects.draw(view);
  REQUIRE(prims.size() == 1);
  const ScenePrim& first = prims.front();
  CHECK(first.blend == ScenePrimBlend::Additive);
  CHECK(first.stpMask);
  // 32 texels at 0x2000 / 4096 are 64 units, 1000 / 2000 of them on screen: 32 pixels.
  CHECK(first.corners[3].offset.x - first.corners[0].offset.x == doctest::Approx(32.0F));
  CHECK(first.color.r == doctest::Approx(1.0F));
  for (int frame = 0; frame < 13; ++frame) {
    effects.tickFrame();
  }
  prims = effects.draw(view);
  REQUIRE(prims.size() == 1);
  // State 13: 0x6400 / 4096 of 32 texels, shade 0x40, column 96.
  CHECK(prims.front().corners[3].offset.x - prims.front().corners[0].offset.x ==
        doctest::Approx(32.0F * 0x6400 / 4096.0F / 2.0F));
  CHECK(prims.front().color.r == doctest::Approx(0.5F));
  CHECK(prims.front().corners[0].uv.x == doctest::Approx(96.0F / 256.0F));
  effects.tickFrame();
  CHECK(effects.clouds().empty());
  CHECK(effects.draw(view).empty());
}

TEST_CASE("field effects: at most 60 clouds and 4 sparks at once") {
  FieldEffects effects{Seed};
  for (int i = 0; i < 70; ++i) {
    effects.addCloud(i, Ahead);
  }
  CHECK(effects.clouds().size() == 60);
  for (int i = 0; i < 6; ++i) {
    effects.addSpark(0, 0, {0, 0, Ahead}, 0);
  }
  CHECK(effects.sparks().size() == 4);
  effects.removeClouds();
  CHECK(effects.clouds().empty());
  // An unknown kind or count makes none (createParticleFX's default cases).
  FieldEffects other{Seed};
  other.addSpark(7, 0, {0, 0, Ahead}, 0);
  other.addSpark(0, 3, {0, 0, Ahead}, 0);
  CHECK(other.sparks().empty());
}

TEST_CASE("field effects: a gym spark flashes on its second frame and spreads 25 particles") {
  // createParticleFX(0, 0, pos, NULL, 0): timer 0x13, endFrame 4; renderParticleFX draws two
  // flashes when the state is 2 and 0x19 particles while it is below the timer.
  FieldEffects effects{Seed};
  effects.addSpark(0, 0, {0, 0, Ahead}, 0);
  const FieldEffectView view = viewOf();
  CHECK(countTexture(effects.draw(view), 4) == 0);
  CHECK(countTexture(effects.draw(view), 2) == 25);
  effects.tickFrame();
  effects.tickFrame();
  std::vector<ScenePrim> prims = effects.draw(view);
  CHECK(countTexture(prims, 4) == 8);
  CHECK(countTexture(prims, 2) == 25);
  effects.tickFrame();
  CHECK(countTexture(effects.draw(view), 4) == 0);
  for (int frame = 3; frame < 0x12; ++frame) {
    effects.tickFrame();
  }
  CHECK(effects.sparks().size() == 1);
  effects.tickFrame();
  CHECK(effects.sparks().empty());
}

TEST_CASE("field effects: a gym's cloud and spark are first drawn a frame on, a pile's as made") {
  // Measured in the original: TRN_tickDefenseTraining's createCloudFX and createParticleFX are run
  // by tickObjects, which then ticks the new objects before renderObjects; createPoopPile's cloud
  // is first drawn at state 0.
  const FieldEffectView view = viewOf();
  FieldEffects gym{Seed};
  gym.addCloud(0, Ahead, FieldEffects::Birth::InObjects);
  gym.addSpark(0, 0, {0, 0, Ahead}, 0, FieldEffects::Birth::InObjects);
  CHECK(gym.clouds().front().state == 1);
  CHECK(gym.sparks().front().state == 1);
  // The spark's flash is on state 2: the next frame.
  CHECK(countTexture(gym.draw(view), 4) == 0);
  gym.tickFrame();
  CHECK(countTexture(gym.draw(view), 4) == 8);
  // It ends after the same 0x13 states, one frame sooner than one made outside the objects.
  for (int frame = 2; frame < 0x12; ++frame) {
    gym.tickFrame();
  }
  CHECK(gym.sparks().size() == 1);
  gym.tickFrame();
  CHECK(gym.sparks().empty());
  FieldEffects pile{Seed};
  pile.addCloud(0, Ahead);
  CHECK(pile.clouds().front().state == 0);
}

TEST_CASE("field effects: the healing spiral drops from over the head and lasts 44 frames") {
  // particle.c: addHealingParticleEffect: y = -height - 70, radius * 12 / 10;
  // tickHealingParticles: one spiral particle a frame for 20 frames, lit 17 frames; the ring only
  // with hasParticle2.
  FieldEffects effects{Seed};
  REQUIRE(effects.addHealing({100, 0, Ahead}, 200, 50, false));
  CHECK_FALSE(effects.addHealing({100, 0, Ahead}, 200, 50, true));
  const FieldEffectView view = viewOf();
  effects.tickFrame();
  REQUIRE(effects.healing());
  const auto& spiral = effects.healing()->spiral;
  CHECK(spiral[1].counter == 16);
  // Frame 1: a turn of 640 4096ths of the 60 radius, the height 270 * 19 / 20 down from the top.
  CHECK(spiral[1].position.y == -270 + 13);
  CHECK(spiral[1].position.x == 100 + ((60 * dwe::core::original::rcos(640)) >> 12));
  CHECK(spiral[1].position.z == Ahead + ((-(60 * dwe::core::original::rsin(640))) >> 12));
  CHECK(spiral[1].size >= 15);
  CHECK(spiral[1].size < 85);
  // Each lit spiral particle draws twice (its glow and its core); no ring.
  CHECK(countTexture(effects.draw(view), 2) == 2);
  for (int frame = 1; frame < 43; ++frame) {
    effects.tickFrame();
  }
  CHECK(effects.healing());
  effects.tickFrame();
  CHECK_FALSE(effects.healing());

  FieldEffects ringed{Seed};
  REQUIRE(ringed.addHealing({0, 0, Ahead}, 200, 50, true));
  ringed.tickFrame();
  // Four ring particles join the spiral's two.
  CHECK(countTexture(ringed.draw(view), 2) == 6);
}

TEST_CASE("field effects: the butterfly flaps every 2 frames and waits in a menu") {
  FieldEffects effects{Seed};
  effects.setButterfly();
  FieldEffectView view = viewOf();
  CHECK(effects.draw(view).empty());
  view.butterflyCentre = glm::vec3{0.0F, 0.0F, -static_cast<float>(Ahead)};
  std::vector<ScenePrim> prims = effects.draw(view);
  REQUIRE(prims.size() == 1);
  CHECK(prims.front().corners[0].uv.x == doctest::Approx(0.0F));
  CHECK(prims.front().stpMask);
  effects.tickFrame();
  effects.tickFrame();
  prims = effects.draw(view);
  REQUIRE(prims.size() == 1);
  CHECK(prims.front().corners[0].uv.x == doctest::Approx(16.0F / 256.0F));
  effects.tickFrame(true);
  CHECK(effects.butterfly()->frame == 2);
  effects.unsetButterfly();
  CHECK(effects.empty());
}

TEST_CASE("object smoke: the objects drift right and down, start over past x 160, hide at 60") {
  // overworld.c: createNinjamonEffect rolls randomLimit(10) + 12 across and + 3 down for each of
  // the 41 objects; renderNinjamonEffect moves them a frame at a time.
  ObjectSmoke smoke{Seed};
  std::vector<glm::ivec2> places(ObjectSmoke::Objects, glm::ivec2{100, 50});
  places.front() = {150, 0};
  smoke.start(places);
  REQUIRE(smoke.active());
  CHECK_FALSE(smoke.frame(places));
  for (std::size_t i = 1; i < places.size(); ++i) {
    CHECK(places[i].x >= 112);
    CHECK(places[i].x < 122);
    CHECK(places[i].y >= 53);
    CHECK(places[i].y < 63);
  }
  // 150 + 12 or more is past 160: back where it was.
  CHECK(places.front() == glm::ivec2{150, 0});
  for (int frame = 1; frame < 59; ++frame) {
    CHECK_FALSE(smoke.frame(places));
  }
  CHECK(smoke.frame(places));
  CHECK_FALSE(smoke.active());
  for (const glm::ivec2& place : places) {
    CHECK(place.x <= 160);
  }
}
