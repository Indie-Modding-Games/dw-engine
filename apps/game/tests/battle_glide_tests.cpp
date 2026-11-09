#include <dwe/animation/animation_player.hpp>
#include <dwe/assets/model_data.hpp>

#include <doctest/doctest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "battle_glide.hpp"
#include "packages/game/battle/tests/fixture.hpp"

using dwe::app::BattleFrameSeconds;
using dwe::app::betweenAngles;
using dwe::app::betweenFrames;
using dwe::app::blendEffects;
using dwe::app::ClipTween;
using dwe::app::cloudBehind;
using dwe::app::PoseGlide;
using dwe::app::SceneEffect;
using dwe::app::sparkBehind;
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

// One joint; clip 0 moves it from 0 to 60 units over a second and does not repeat, clip 1 turns
// it from 100 back to 0 over half a second and repeats. Made-up clips.
dwe::assets::ModelData twoClipModel() {
  dwe::assets::ModelData model;
  model.nodes = {{.name = "bone_00", .parent = std::nullopt, .rest = {}}};
  model.nodeOrder = {0};
  const auto clip = [](std::uint32_t id, float seconds, float from, float to) {
    dwe::assets::AnimationClip made;
    made.name = "made_up";
    made.id = id;
    made.duration = seconds;
    made.ticksPerSecond = 20;
    made.tickCount = static_cast<std::uint32_t>(seconds * 20.0F) + 1;
    made.channels.push_back({.node = 0,
                             .path = dwe::assets::ChannelPath::Translation,
                             .times = {0.0F, seconds},
                             .values = {{0, 0, from, 0}, {0, 0, to, 0}}});
    return made;
  };
  model.clips.push_back(clip(0, 1.0F, 0.0F, 60.0F));
  dwe::assets::AnimationClip back = clip(1, 0.5F, 100.0F, 0.0F);
  back.repeat = dwe::assets::ClipRepeat{.start = 0.0F, .end = 0.5F, .gap = 0.0F};
  model.clips.push_back(back);
  return model;
}

// Where the clip puts the one joint (a model without a skin draws with one identity matrix, so
// the pose is what moves).
float jointZ(std::span<const dwe::assets::NodeTransform> pose) {
  return pose.empty() ? 0.0F : pose.front().translation.z;
}

// FNV-1a over the bytes of what the battle's logic reads back from the drawing.
struct Hash {
  std::uint64_t value = 0xCBF29CE484222325ULL;
  void add(std::uint64_t word) {
    for (int byte = 0; byte < 8; ++byte) {
      value = (value ^ ((word >> (byte * 8)) & 0xFFU)) * 0x100000001B3ULL;
    }
  }
  void add(float number) { add(static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(number))); }
  void add(double number) { add(std::bit_cast<std::uint64_t>(number)); }
};

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

TEST_CASE("a held fighter's clip is drawn between its last two ticks, one tick behind") {
  const dwe::assets::ModelData model = twoClipModel();
  dwe::animation::AnimationPlayer player{model};
  ClipTween tween;
  REQUIRE(player.play(0U));
  tween.follow(player, false, Tick);
  // Two ticks run: the clip is at 2/60 s (2 units); the drawing goes from 1 to 2 over the next.
  static_cast<void>(player.advance(static_cast<float>(Tick)));
  tween.follow(player, true, Tick);
  static_cast<void>(player.advance(static_cast<float>(Tick)));
  tween.follow(player, true, Tick);
  tween.pose(model, 0.0F);
  CHECK(jointZ(tween.nodes()) == doctest::Approx(1.0F));
  tween.pose(model, 0.5F);
  CHECK(jointZ(tween.nodes()) == doctest::Approx(1.5F));
  tween.pose(model, 1.0F);
  CHECK(jointZ(tween.nodes()) == doctest::Approx(2.0F));
  CHECK(jointZ(player.pose()) == doctest::Approx(2.0F));
  // A frame drawn without a tick keeps the same two ticks.
  tween.follow(player, false, Tick);
  CHECK(tween.timeAt(0.5F) == doctest::Approx(1.5 * Tick));
  // A frozen clip (no time, though ticks ran) stands still at every alpha.
  tween.follow(player, true, Tick);
  CHECK(tween.timeAt(0.0F) == doctest::Approx(tween.timeAt(1.0F)));
  // Three ticks in one drawn frame: only the last one is drawn on the way.
  static_cast<void>(player.advance(static_cast<float>(3.0 * Tick)));
  tween.follow(player, true, Tick);
  CHECK(tween.timeAt(0.0F) == doctest::Approx(4.0 * Tick));
}

TEST_CASE("a fighter's new clip, or the same one started over, is drawn from its own start") {
  const dwe::assets::ModelData model = twoClipModel();
  dwe::animation::AnimationPlayer player{model};
  ClipTween tween;
  REQUIRE(player.play(0U));
  for (int tick = 0; tick < 30; ++tick) {
    static_cast<void>(player.advance(static_cast<float>(Tick)));
    tween.follow(player, true, Tick);
  }
  // Clip 1 starts (at 100 units) and runs a tick: nothing of clip 0's 30 units is mixed in.
  REQUIRE(player.play(1U));
  static_cast<void>(player.advance(static_cast<float>(Tick)));
  tween.follow(player, true, Tick);
  tween.pose(model, 0.0F);
  CHECK(jointZ(tween.nodes()) == doctest::Approx(jointZ(player.pose())));
  // Clip 1 again from its start: the drawing does not run back over the last pass.
  for (int tick = 0; tick < 10; ++tick) {
    static_cast<void>(player.advance(static_cast<float>(Tick)));
    tween.follow(player, true, Tick);
  }
  REQUIRE(player.play(1U));
  static_cast<void>(player.advance(static_cast<float>(Tick)));
  tween.follow(player, true, Tick);
  CHECK(tween.timeAt(0.0F) == doctest::Approx(Tick));
  CHECK(tween.timeAt(1.0F) == doctest::Approx(Tick));
}

TEST_CASE("the status visuals' counts are drawn between frames only when they took one step") {
  CHECK(betweenFrames(4, 5, 0.25F) == doctest::Approx(4.25F));
  CHECK(betweenFrames(4, 5, 1.0F) == doctest::Approx(5.0F));
  // A start, an end or a jump: this frame's.
  CHECK(betweenFrames(9, 2, 0.25F) == doctest::Approx(2.0F));
  CHECK(betweenFrames(2, 2, 0.25F) == doctest::Approx(2.0F));
  // A life that counts down.
  CHECK(betweenFrames(6, 5, 0.5F, -1) == doctest::Approx(5.5F));
  // An angle the short way round: from 500 to 10 of 512 goes through 0.
  CHECK(betweenAngles(500.0F, 10.0F, 512.0F, 0.5F) == doctest::Approx(-1.0F));
}

TEST_CASE("a hit spark and a cloud are drawn behind their frame only when they were there before") {
  using battle::HitParticles;
  dwe::ruleset::Random random{7};
  HitParticles last;
  last.setNodeCounts({10});
  last.spawn(random, HitParticles::Spark::Hit, 1, {0, -100, 0}, 0, 0x20);
  HitParticles now = last;
  now.tick(random);
  REQUIRE(now.sparkSlots()[0].has_value());
  CHECK(sparkBehind(&last, 0, *now.sparkSlots()[0], 0.25F) == doctest::Approx(0.75F));
  CHECK(sparkBehind(&last, 0, *now.sparkSlots()[0], 1.0F) == doctest::Approx(0.0F));
  CHECK(sparkBehind(nullptr, 0, *now.sparkSlots()[0], 0.25F) == doctest::Approx(0.0F));
  // A spark that just burst in its slot was not there on the last frame.
  CHECK(sparkBehind(&now, 0, *last.sparkSlots()[0], 0.25F) == doctest::Approx(0.0F));
  const HitParticles::Cloud cloud{.x = 10, .z = 20, .state = 3};
  CHECK(cloudBehind(&now, cloud, 0.5F) == doctest::Approx(0.0F));
}

// The game's loop with the battle in it, as the scene draws it: 60 ticks a second, each running
// the battle on (Battle::advance) and holding its fighters (FieldScene::holdFighter, runFighters),
// and frames drawn at an uneven 144 a second that run the fighters' clips on (animateFighter)
// and, with smooth motion, draw between the ticks (PoseGlide, ClipTween, blendEffects' and the
// status visuals' helpers). Each tick hashes what the battle's logic reads: the battle's own
// checksum and, from the drawing, the fighters' clip time and pose (fighterPlayedThrough,
// fighterPose).
std::vector<std::uint64_t> battleTicks(bool smooth, int* drawnBetween) {
  using namespace battle_test;
  const dwe::assets::ModelData model = twoClipModel();
  Battle fight = startBattle(
      duel(evenStats(2000, 120), evenStats(2000, 120), {"punch", "shot"}, {"punch"}), 3);
  struct Figure {
    std::unique_ptr<dwe::animation::AnimationPlayer> player;
    std::optional<std::pair<std::int32_t, std::uint32_t>> clip;
    PoseGlide glide;
    ClipTween tween;
  };
  std::vector<Figure> figures(fight.state().fighters.size());
  for (Figure& figure : figures) {
    figure.player = std::make_unique<dwe::animation::AnimationPlayer>(model);
  }
  std::optional<battle::HitParticles> lastSparks;
  std::vector<std::uint64_t> ticks;
  double due = 0.0;
  double accumulated = 0.0;
  // Uneven frames around 144 a second.
  const std::array<double, 4> frames{1.0 / 144.0, 1.0 / 100.0, 1.0 / 200.0, 1.0 / 144.0};
  for (std::size_t frame = 0; ticks.size() < 1800 && !fight.finished(); ++frame) {
    accumulated += frames.at(frame % frames.size());
    while (accumulated >= Tick) {
      accumulated -= Tick;
      const std::int64_t before = fight.state().frame;
      battle::HitParticles sparksBefore = fight.hitParticles();
      static_cast<void>(fight.advance(Tick));
      if (fight.state().frame != before) {
        lastSparks = std::move(sparksBefore);
      }
      Hash hash;
      hash.add(fight.checksum());
      for (std::size_t id = 0; id < figures.size(); ++id) {
        Figure& figure = figures[id];
        const battle::Fighter& fighter = fight.state().fighters[id];
        figure.glide.advance(Tick);
        figure.glide.moveTo({.position = {static_cast<float>(fighter.position.x), 0.0F,
                                          static_cast<float>(fighter.position.z)},
                             .yaw = 0.0F});
        const std::pair shown{fighter.shown.clip, fighter.shown.starts};
        if (figure.clip != shown) {
          figure.clip = shown;
          REQUIRE(figure.player->play(
              model.clips[static_cast<std::size_t>(fighter.shown.clip) % model.clips.size()]));
        }
        hash.add(figure.player->time());
        hash.add(static_cast<std::uint64_t>(figure.player->playedThrough()));
        for (const dwe::assets::NodeTransform& node : figure.player->pose()) {
          hash.add(node.translation.z);
          hash.add(node.rotation.w);
        }
      }
      due += Tick;
      ticks.push_back(hash.value);
    }
    // The drawn frame.
    const auto alpha = static_cast<float>(accumulated / Tick);
    for (std::size_t id = 0; id < figures.size(); ++id) {
      Figure& figure = figures[id];
      static_cast<void>(figure.player->advance(
          fight.state().fighters[id].shown.frozen ? 0.0F : static_cast<float>(due)));
      figure.tween.follow(*figure.player, due > 0.0, Tick);
      if (!smooth) {
        continue;
      }
      figure.tween.pose(model, alpha);
      static_cast<void>(figure.glide.drawn(static_cast<double>(alpha) * Tick));
      if (figure.tween.posed() &&
          std::abs(jointZ(figure.tween.nodes()) - jointZ(figure.player->pose())) > 0.01F) {
        ++*drawnBetween;
      }
      const auto& sparks = fight.hitParticles().sparkSlots();
      for (std::size_t slot = 0; slot < sparks.size(); ++slot) {
        if (sparks[slot] && lastSparks) {
          static_cast<void>(sparkBehind(&*lastSparks, slot, *sparks[slot], alpha));
        }
      }
    }
    due = 0.0;
  }
  return ticks;
}

TEST_CASE("smooth motion changes only the drawing: the battle's ticks hash alike on and off") {
  int drawnBetween = 0;
  const std::vector<std::uint64_t> off = battleTicks(false, &drawnBetween);
  REQUIRE(drawnBetween == 0);
  const std::vector<std::uint64_t> on = battleTicks(true, &drawnBetween);
  // The tween did draw poses between the ticks, and the logic saw the same ticks.
  CHECK(drawnBetween > 100);
  REQUIRE(on.size() == off.size());
  CHECK(on.size() > 600);
  CHECK(on == off);
}
