#include <dwe/animation/animation_player.hpp>
#include <dwe/animation/clip_sampling.hpp>
#include <dwe/assets/character_model.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <numbers>
#include <optional>
#include <vector>

using dwe::animation::AnimationPlayer;
using dwe::animation::locate;
using dwe::animation::rootMotion;
using dwe::animation::sampleClip;
using dwe::assets::AnimationClip;
using dwe::assets::ChannelPath;
using dwe::assets::ModelData;
using dwe::assets::NodeTransform;

namespace {

constexpr float TicksPerSecond = 20.0F;

double ticks(double n) {
  return n / static_cast<double>(TicksPerSecond);
}

glm::vec4 turnY(float degrees) {
  const glm::quat q = glm::angleAxis(glm::radians(degrees), glm::vec3{0, 1, 0});
  return {q.x, q.y, q.z, q.w};
}

// A root joint and a child 10 units up. Clip 2 is like an extracted walk: five ticks, the root
// moving 10 units a tick along +Z and the child turning 10 degrees a tick, repeating forever from
// tick 1 to tick 4. Clip 5 does not repeat: it lifts the child 10 units over two ticks.
ModelData testModel() {
  ModelData model;
  model.nodes = {{.name = "bone_00", .parent = std::nullopt, .rest = {}},
                 {.name = "bone_01", .parent = 0U, .rest = {.translation = {0, 10, 0}}}};
  model.nodeOrder = {0, 1};
  model.skin = dwe::assets::ModelSkin{
      .joints = {0, 1},
      .inverseBindMatrices = {glm::mat4{1.0F},
                              glm::translate(glm::mat4{1.0F}, glm::vec3{0, -10, 0})}};
  std::vector<float> times;
  std::vector<glm::vec4> root;
  std::vector<glm::vec4> turn;
  for (int t = 0; t < 5; ++t) {
    times.push_back(static_cast<float>(t) / TicksPerSecond);
    root.emplace_back(0, 0, 10 * t, 0);
    turn.push_back(turnY(10.0F * static_cast<float>(t)));
  }
  AnimationClip walk{.name = "anim_02",
                     .id = 2,
                     .duration = times.back(),
                     .ticksPerSecond = 20,
                     .tickCount = 5,
                     .frameCount = 5,
                     .loops = {{.firstTick = 1, .lastTick = 4, .count = std::nullopt}},
                     .endsInLoop = true,
                     .repeat = dwe::assets::ClipRepeat{.start = 1 / TicksPerSecond,
                                                       .end = 4 / TicksPerSecond,
                                                       .gap = 1 / TicksPerSecond},
                     .rootMotionNode = 0,
                     .channels = {},
                     .sounds = {}};
  walk.channels.push_back(
      {.node = 0, .path = ChannelPath::Translation, .times = times, .values = root});
  walk.channels.push_back(
      {.node = 1, .path = ChannelPath::Rotation, .times = times, .values = turn});
  model.clips.push_back(walk);

  AnimationClip lift;
  lift.name = "anim_05";
  lift.id = 5;
  lift.duration = 2 / TicksPerSecond;
  lift.ticksPerSecond = 20;
  lift.tickCount = 3;
  lift.rootMotionNode = 0;
  lift.channels.push_back({.node = 1,
                           .path = ChannelPath::Translation,
                           .times = {0, 1 / TicksPerSecond, 2 / TicksPerSecond},
                           .values = {{0, 10, 0, 0}, {0, 15, 0, 0}, {0, 20, 0, 0}}});
  model.clips.push_back(lift);
  return model;
}

float angleY(const glm::quat& q) {
  return glm::degrees(2.0F * std::atan2(q.y, q.w));
}

} // namespace

TEST_CASE("a clip sampled at tick N gives keyframe N") {
  const ModelData model = testModel();
  const AnimationClip& walk = *model.findClip(2U);
  std::vector<NodeTransform> pose(model.nodes.size());
  for (int n = 0; n < 5; ++n) {
    sampleClip(model, walk, ticks(n), pose);
    CHECK(angleY(pose[1].rotation) == doctest::Approx(10.0F * static_cast<float>(n)).epsilon(1e-4));
    // The root motion node keeps its rest translation; the movement goes to rootMotion.
    CHECK(pose[0].translation == glm::vec3{0.0F});
    CHECK(pose[1].translation == glm::vec3{0, 10, 0});
  }
  sampleClip(model, walk, ticks(2.5), pose);
  CHECK(angleY(pose[1].rotation) == doctest::Approx(25.0F).epsilon(1e-4));
}

TEST_CASE("a clip that ends in a loop goes round its repeat") {
  const ModelData model = testModel();
  const AnimationClip& walk = *model.findClip(2U);
  std::vector<NodeTransform> pose(model.nodes.size());

  // After tick 4 comes tick 1 again: tick 5 is tick 1, tick 8 is tick 4, tick 9 is tick 1.
  for (const auto& [time, keyframe] :
       std::vector<std::pair<double, float>>{{5, 1}, {6, 2}, {8, 4}, {9, 1}, {13, 1}}) {
    CAPTURE(time);
    sampleClip(model, walk, ticks(time), pose);
    CHECK(angleY(pose[1].rotation) == doctest::Approx(10.0F * keyframe).epsilon(1e-4));
  }
  // Between tick 4 and the next tick 1 the pose blends keyframe 4 (40 degrees) into keyframe 1 (10
  // degrees).
  sampleClip(model, walk, ticks(4.5), pose);
  CHECK(angleY(pose[1].rotation) == doctest::Approx(25.0F).epsilon(1e-4));

  const auto at = locate(walk, ticks(10.5));
  CHECK(at.repeats == 2);
  CHECK(at.seconds == doctest::Approx(ticks(2.5)));
  CHECK(at.blend == 0.0F);
}

TEST_CASE("root motion keeps going through every repeat") {
  const ModelData model = testModel();
  const AnimationClip& walk = *model.findClip(2U);
  // 10 units a tick, before the loop, in the step back and in every round of it.
  for (const double t : {0.0, 1.0, 3.5, 4.0, 4.5, 5.0, 7.25, 20.0, 1001.0}) {
    CAPTURE(t);
    const glm::vec3 moved = rootMotion(model, walk, ticks(t));
    CHECK(moved.z == doctest::Approx(10.0 * t).epsilon(1e-4));
    CHECK(moved.x == 0.0F);
  }
  CHECK(dwe::animation::repeatGroundSpeed(model, walk) == doctest::Approx(200.0F));
  CHECK(dwe::animation::repeatGroundSpeed(model, *model.findClip(5U)) == 0.0F);
}

TEST_CASE("a loop over the whole clip does not move the root in its step back") {
  const ModelData model = testModel();
  AnimationClip whole = *model.findClip(2U);
  whole.repeat =
      dwe::assets::ClipRepeat{.start = 0.0F, .end = 4 / TicksPerSecond, .gap = 1 / TicksPerSecond};
  // One round: ticks 0 to 4 move 40 units, then the step back to tick 0 holds still.
  CHECK(rootMotion(model, whole, ticks(4.5)).z == doctest::Approx(40.0F));
  CHECK(rootMotion(model, whole, ticks(5.0)).z == doctest::Approx(40.0F));
  CHECK(rootMotion(model, whole, ticks(6.0)).z == doctest::Approx(50.0F));
  CHECK(dwe::animation::repeatGroundSpeed(model, whole) == doctest::Approx(160.0F));
}

TEST_CASE("a clip that does not repeat holds its last pose") {
  const ModelData model = testModel();
  std::vector<NodeTransform> pose(model.nodes.size());
  sampleClip(model, *model.findClip(5U), ticks(1.5), pose);
  CHECK(pose[1].translation.y == doctest::Approx(17.5F));
  sampleClip(model, *model.findClip(5U), 10.0, pose);
  CHECK(pose[1].translation.y == doctest::Approx(20.0F));

  AnimationPlayer player{model};
  REQUIRE(player.play(5U));
  CHECK_FALSE(player.finished());
  player.advance(1.0F);
  CHECK(player.finished());
  CHECK_FALSE(player.play(9U));

  // A clip of another model is refused, and the player keeps its clip.
  const ModelData other = testModel();
  CHECK_FALSE(player.play(*other.findClip(2U)));
  CHECK(player.clip() == model.findClip(5U));
}

TEST_CASE("a clip that repeats has played through at the end of its first pass") {
  const ModelData model = testModel();
  AnimationPlayer player{model};
  REQUIRE(player.play(2U));
  const float pass = model.findClip(2U)->duration;
  player.advance(pass * 0.5F);
  CHECK_FALSE(player.playedThrough());
  player.advance(pass);
  CHECK(player.playedThrough());
  CHECK_FALSE(player.finished());
}

TEST_CASE("joint matrices move the vertices with their joints") {
  const ModelData model = testModel();
  AnimationPlayer player{model};
  // At rest every joint matrix is the identity: the vertices are stored in the bind pose.
  for (const glm::mat4& joint : player.jointMatrices()) {
    CHECK(joint == glm::mat4{1.0F});
  }
  REQUIRE(player.play(2U));
  player.advance(static_cast<float>(ticks(3)));
  // The child joint turned 30 degrees about its own origin, 10 units up: a vertex at (1, 10, 0)
  // follows it.
  const glm::vec4 moved = player.jointMatrices()[1] * glm::vec4{1, 10, 0, 1};
  const float angle = glm::radians(30.0F);
  CHECK(moved.x == doctest::Approx(std::cos(angle)));
  CHECK(moved.y == doctest::Approx(10.0F));
  CHECK(moved.z == doctest::Approx(-std::sin(angle)));
}

TEST_CASE("the player reports the root motion of each step") {
  const ModelData model = testModel();
  AnimationPlayer player{model};
  REQUIRE(player.play(2U, 0.0F, 2.0F));
  glm::vec3 total{0.0F};
  for (int frame = 0; frame < 60; ++frame) {
    total += player.advance(1.0F / 60.0F);
  }
  // One second at twice the speed: 40 ticks of 10 units.
  CHECK(total.z == doctest::Approx(400.0F).epsilon(1e-4));
  CHECK(player.time() == doctest::Approx(2.0));
}

TEST_CASE("a clip plays its sounds on the ticks it leaves, on every round of its repeat") {
  // The player's run as the original has it (boys anim 3): 13 ticks repeating from 1 to 12, a step
  // at ticks 2 and 8 (bank 1, sound 27). The original plays one every 6 frames (18 vblanks).
  AnimationClip run{
      .name = "anim_03",
      .id = 3,
      .duration = 12 / TicksPerSecond,
      .ticksPerSecond = 20,
      .tickCount = 13,
      .frameCount = 13,
      .loops = {{.firstTick = 1, .lastTick = 12, .count = std::nullopt}},
      .endsInLoop = true,
      .repeat = dwe::assets::ClipRepeat{.start = 1 / TicksPerSecond,
                                        .end = 12 / TicksPerSecond,
                                        .gap = 1 / TicksPerSecond},
      .rootMotionNode = std::nullopt,
      .channels = {},
      .sounds = {{.tick = 2, .bank = 1, .sound = 27}, {.tick = 8, .bank = 1, .sound = 27}}};
  std::vector<int> stepFrames;
  double time = 0.0;
  for (int frame = 0; frame < 30; ++frame) {
    std::vector<dwe::assets::ClipSound> played;
    dwe::animation::clipSounds(run, time, time + ticks(1), played);
    time += ticks(1);
    for (const dwe::assets::ClipSound& sound : played) {
      CHECK(sound.bank == 1);
      CHECK(sound.sound == 27);
      stepFrames.push_back(frame);
    }
  }
  CHECK(stepFrames == std::vector<int>{2, 8, 14, 20, 26});

  // A long step reaches each tick of a round once; a clip that does not repeat plays its sounds
  // once.
  std::vector<dwe::assets::ClipSound> long_;
  dwe::animation::clipSounds(run, ticks(13), ticks(1000), long_);
  CHECK(long_.size() == 2);
  AnimationClip once = run;
  once.repeat.reset();
  once.endsInLoop = false;
  std::vector<dwe::assets::ClipSound> after;
  dwe::animation::clipSounds(once, ticks(9), ticks(1000), after);
  CHECK(after.empty());
}

TEST_CASE("the player hands over the sounds of its last step once") {
  ModelData model = testModel();
  model.clips[0].sounds = {{.tick = 2, .bank = 1, .sound = 12}};
  AnimationPlayer player{model};
  REQUIRE(player.play(2U));
  static_cast<void>(player.advance(static_cast<float>(ticks(2))));
  CHECK(player.takeSounds().empty());
  static_cast<void>(player.advance(static_cast<float>(ticks(1))));
  CHECK(player.takeSounds() ==
        std::vector<dwe::assets::ClipSound>{{.tick = 2, .bank = 1, .sound = 12}});
  CHECK(player.takeSounds().empty());
  // Round 2 of the repeat (ticks 1 to 4, then back): tick 2 again 4 ticks later.
  static_cast<void>(player.advance(static_cast<float>(ticks(3))));
  CHECK(player.takeSounds().empty());
  static_cast<void>(player.advance(static_cast<float>(ticks(1))));
  CHECK(player.takeSounds().size() == 1);
}

TEST_CASE("a fade blends from the previous pose") {
  const ModelData model = testModel();
  AnimationPlayer player{model};
  REQUIRE(player.play(2U));
  player.advance(static_cast<float>(ticks(4)));
  CHECK(angleY(player.pose()[1].rotation) == doctest::Approx(40.0F).epsilon(1e-4));
  REQUIRE(player.play(5U, 0.1F));
  CHECK(angleY(player.pose()[1].rotation) == doctest::Approx(40.0F).epsilon(1e-4));
  player.advance(0.05F);
  CHECK(angleY(player.pose()[1].rotation) == doctest::Approx(20.0F).epsilon(1e-3));
  player.advance(0.05F);
  CHECK(angleY(player.pose()[1].rotation) == doctest::Approx(0.0F));
}

TEST_CASE("the game pack's character models parse") {
  const dwe::assets::DiskFileSystem disk;
  for (const char* name : {"tamer", "partner"}) {
    CAPTURE(name);
    const auto bytes = disk.readFile(std::filesystem::path{DWE_GAME_PACK_DIR} / "models" /
                                     std::format("{}.yaml", name));
    REQUIRE(bytes.has_value());
    const auto character = dwe::assets::parseCharacterModel(
        {reinterpret_cast<const char*>(bytes->data()), bytes->size()});
    REQUIRE_MESSAGE(character.has_value(), (character ? "" : character.error().message));
    for (const char* animation : {"idle", "walk", "run"}) {
      CHECK(character->clipOf(animation).has_value());
    }
  }
}

// Runs only when DWE_CONTENT names a content folder (scripts/dev.sh mounts it). Mounts it with the
// game pack and checks the player and the starting partner against what the original does with them
// (docs/architecture/models.md): every animation has its clip, and the walk and run cycles cover
// the ground at these speeds, in units per second.
TEST_CASE(
    "the user's disc, extracted: the characters' walk and run cycles move as in the original") {
  const std::optional<std::filesystem::path> mountedContent = dwe::test_support::userContent();
  if (!mountedContent) {
    MESSAGE("DWE_CONTENT is not set: skipping the test on the extracted content");
    return;
  }
  const std::filesystem::path& content = *mountedContent;
  const dwe::assets::DiskFileSystem disk;
  auto packs = dwe::assets::findPacks(disk, content);
  REQUIRE(packs.has_value());
  packs->packs.push_back({.path = DWE_GAME_PACK_DIR, .kind = dwe::assets::FoundPack::Kind::Folder});
  const auto opened = dwe::assets::openPacks(disk, packs->packs, dwe::assets::PackOrigin::Engine);
  REQUIRE_MESSAGE(opened.has_value(), (opened ? "" : opened.error().message));
  const auto vfs = dwe::assets::VirtualFileSystem::mount(opened->locations);
  REQUIRE_MESSAGE(vfs.has_value(), (vfs ? "" : vfs.error().message));

  struct Expected {
    const char* character;
    float walk;
    float run;
  };
  for (const Expected& expected :
       {Expected{.character = "models/tamer", .walk = 400.0F, .run = 1000.0F},
        Expected{.character = "models/partner", .walk = 450.0F, .run = 900.0F}}) {
    CAPTURE(expected.character);
    const auto character =
        dwe::assets::loadCharacterModel(*vfs, *dwe::assets::AssetId::parse(expected.character));
    REQUIRE_MESSAGE(character.has_value(), (character ? "" : character.error().message));
    const auto model = dwe::assets::loadModel(*vfs, character->model);
    REQUIRE(model.has_value());
    REQUIRE(dwe::assets::checkCharacterModel(*character, *model).has_value());
    const auto speed = [&](const char* animation) {
      return dwe::animation::repeatGroundSpeed(*model,
                                               *model->findClip(*character->clipOf(animation)));
    };
    CHECK(speed("idle") == 0.0F);
    CHECK(speed("walk") == doctest::Approx(expected.walk).epsilon(1e-3));
    CHECK(speed("run") == doctest::Approx(expected.run).epsilon(1e-3));

    // A minute of every animation at 60 frames a second: the joints stay finite and the root keeps
    // its pace.
    for (const auto& [animation, clipId] : character->animations) {
      CAPTURE(animation);
      AnimationPlayer player{*model};
      REQUIRE(player.play(clipId));
      glm::vec3 moved{0.0F};
      for (int frame = 0; frame < 3600; ++frame) {
        moved += player.advance(1.0F / 60.0F);
      }
      for (const glm::mat4& joint : player.jointMatrices()) {
        CHECK(std::isfinite(joint[3][0] + joint[3][1] + joint[3][2]));
      }
      if (animation == "walk" || animation == "run") {
        CHECK(glm::length(moved) ==
              doctest::Approx(60.0F * speed(animation.c_str())).epsilon(0.02));
      }
    }
  }
}
