#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "battle_glide.hpp"

using dwe::app::BattleFrameSeconds;
using dwe::app::blendEffects;
using dwe::app::PoseGlide;
using dwe::app::SceneEffect;
namespace battle = dwe::game::battle;

namespace {

constexpr double Tick = 1.0 / 60.0;

SceneEffect sprite(std::string move, std::int32_t x) {
  return {.move = std::move(move),
          .shape = battle::EffectSprite{.position = {.x = x, .y = 0, .z = 0}},
          .palettes = {}};
}

std::int32_t spriteX(const SceneEffect& effect) {
  return std::get<battle::EffectSprite>(effect.shape).position.x;
}

} // namespace

TEST_CASE("a fighter glides over the battle's frame from where it was drawn, one frame behind") {
  PoseGlide glide;
  CHECK_FALSE(glide.started());
  // The first place is drawn at once.
  glide.moveTo({.position = {0.0F, 0.0F, 0.0F}, .yaw = 0.0F});
  CHECK(glide.drawn(0.0).position.x == doctest::Approx(0.0F));
  // A frame moves it 30 units: three ticks of 60 a second draw a third of the way each.
  glide.advance(Tick);
  glide.moveTo({.position = {30.0F, 0.0F, 0.0F}, .yaw = 0.0F});
  CHECK(glide.drawn(0.0).position.x == doctest::Approx(0.0F));
  CHECK(glide.drawn(Tick * 0.5).position.x == doctest::Approx(5.0F));
  glide.advance(Tick);
  // The same place again (a tick with no frame) goes on gliding.
  glide.moveTo({.position = {30.0F, 0.0F, 0.0F}, .yaw = 0.0F});
  CHECK(glide.drawn(0.0).position.x == doctest::Approx(10.0F));
  glide.advance(Tick);
  CHECK(glide.drawn(0.0).position.x == doctest::Approx(20.0F));
  glide.advance(Tick);
  CHECK(glide.drawn(0.0).position.x == doctest::Approx(30.0F));
  // It never draws past the frame's place.
  CHECK(glide.drawn(Tick).position.x == doctest::Approx(30.0F));
  CHECK(BattleFrameSeconds == doctest::Approx(3.0 * Tick));
}

TEST_CASE("a fighter's turn glides the short way and a jump is drawn at once") {
  PoseGlide glide;
  glide.moveTo({.position = {}, .yaw = 3.0F});
  glide.moveTo({.position = {}, .yaw = -3.0F});
  // From 3 to -3 radians the short way is through pi, not through 0.
  const float halfway = glide.drawn(BattleFrameSeconds / 2.0).yaw;
  CHECK(std::abs(halfway) > 3.0F);
  glide.advance(BattleFrameSeconds);
  glide.moveTo({.position = {PoseGlide::JumpDistance + 1.0F, 0.0F, 0.0F}, .yaw = 0.0F});
  CHECK(glide.drawn(0.0).position.x == doctest::Approx(PoseGlide::JumpDistance + 1.0F));
}

TEST_CASE("the effects glide between two frames when each technique draws the same things") {
  const std::vector<SceneEffect> before{sprite("fire", 0), sprite("fire", 100), sprite("ice", 0)};
  const std::vector<SceneEffect> after{sprite("fire", 40), sprite("fire", 200), sprite("ice", 60),
                                       sprite("ice", 90)};
  const std::vector<SceneEffect> half = blendEffects(before, after, 0.5F);
  REQUIRE(half.size() == after.size());
  // The fire's two sprites pair in order.
  CHECK(spriteX(half[0]) == 20);
  CHECK(spriteX(half[1]) == 150);
  // The ice drew one more sprite this frame: it is drawn as the frame has it.
  CHECK(spriteX(half[2]) == 60);
  CHECK(spriteX(half[3]) == 90);
  // At the frame's end, and with no last frame, the frame's own places.
  CHECK(spriteX(blendEffects(before, after, 1.0F)[0]) == 40);
  CHECK(spriteX(blendEffects({}, after, 0.5F)[0]) == 40);
}

TEST_CASE("an effect model glides its translation and keeps the frame's turn") {
  battle::EffectModel from{};
  from.transform.t = {.x = 0, .y = -100, .z = 0};
  battle::EffectModel to{};
  to.transform.t = {.x = 100, .y = -100, .z = 50};
  to.transform.m[0][1] = 2048;
  const std::vector<SceneEffect> before{{.move = "bolt", .shape = from, .palettes = {}}};
  const std::vector<SceneEffect> after{{.move = "bolt", .shape = to, .palettes = {}}};
  const auto quarter = blendEffects(before, after, 0.25F);
  const auto& model = std::get<battle::EffectModel>(quarter.at(0).shape);
  CHECK(model.transform.t.x == 25);
  CHECK(model.transform.t.z == 13);
  CHECK(model.transform.m[0][1] == 2048);
}
