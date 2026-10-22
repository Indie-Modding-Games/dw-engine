#include <dwe/formats/model/animation.hpp>
#include <dwe/formats/model/mmd.hpp>
#include <dwe/formats/model/skeleton.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "model_fixtures.hpp"

using namespace dwe::formats::model;
using namespace dwe::formats::model::testing;

namespace {

std::vector<std::byte> stream(std::initializer_list<std::int32_t> instructions) {
  ByteWriter s;
  s.u16(10);
  for (std::size_t bone = 1; bone < SampleBoneCount; ++bone) {
    s.words({0, 0, 0, 0, 0, 0});
  }
  s.words(instructions);
  return s.bytes();
}

} // namespace

TEST_CASE("an MMD is read with its model and animation table") {
  const auto tmd = buildSampleTmd();
  const auto bytes =
      buildMmd(tmd, {buildSampleAnimationWithLoop(), {}, buildSampleAnimationForever()});
  const auto mmd = readMmd(bytes);
  REQUIRE(mmd.has_value());
  CHECK(mmd->tmd.objects.size() == 2);
  CHECK(mmd->animTableOffset == 8 + tmd.size());
  REQUIRE(mmd->animations.size() == 3);
  CHECK(mmd->animations[0] == mmd->animTableOffset + 12);
  CHECK_FALSE(mmd->animations[1].has_value());
  CHECK(mmd->animations[2].has_value());
}

TEST_CASE("a malformed MMD is an error") {
  const auto tmd = buildSampleTmd();
  const auto good =
      buildMmd(tmd, {buildSampleAnimationWithLoop(), {}, buildSampleAnimationForever()});
  const std::size_t table = 8 + tmd.size();

  CHECK_FALSE(readMmd(std::span(good).first(4)).has_value());

  auto badModel = good;
  ByteWriter header;
  header.u32(4);
  std::copy(header.bytes().begin(), header.bytes().end(), badModel.begin());
  CHECK_FALSE(readMmd(badModel).has_value());

  auto tablePastEnd = good;
  ByteWriter tableOffset;
  tableOffset.u32(static_cast<std::uint32_t>(good.size() + 4));
  std::copy(tableOffset.bytes().begin(), tableOffset.bytes().end(), tablePastEnd.begin() + 4);
  CHECK_FALSE(readMmd(tablePastEnd).has_value());

  ByteWriter entryPastEnd;
  entryPastEnd.append(good);
  entryPastEnd.patchU32(table, static_cast<std::uint32_t>(good.size()));
  CHECK_FALSE(readMmd(entryPastEnd.bytes()).has_value());

  ByteWriter oddEntry;
  oddEntry.append(good);
  oddEntry.patchU32(table, 13);
  CHECK_FALSE(readMmd(oddEntry.bytes()).has_value());

  ByteWriter intoTable;
  intoTable.append(good);
  intoTable.patchU32(table + 4, 4);
  CHECK_FALSE(readMmd(intoTable.bytes()).has_value());

  CHECK_FALSE(readMmd(buildMmd(tmd, {{}, {}})).has_value());
}

TEST_CASE("an animation is played tick by tick as the original does") {
  const auto bytes = buildSampleAnimationWithLoop();
  const auto animation = simulateAnimation(bytes, 0, SampleBoneCount);
  REQUIRE(animation.has_value());
  CHECK(animation->hasScale);
  CHECK(animation->frameCount == 10);
  CHECK_FALSE(animation->endsInLoop);
  // Frames 1 to 8, back to 5, then 6 to 10.
  REQUIRE(animation->tickCount() == 13);
  REQUIRE(animation->poses.size() == 13 * SampleBoneCount);

  // The first tick shows the initial pose: keyframes act from the next tick.
  CHECK(animation->pose(0, 0) ==
        BonePose{.scale = {4096, 4096, 4096}, .rotation = {}, .location = {}});
  CHECK(animation->pose(0, 1).rotation == std::array<std::int16_t, 3>{0, 100, 0});
  CHECK(animation->pose(0, 2).scale == std::array<std::int32_t, 3>{2048, 4096, 8192});

  // 10 over a divisor of 4: 2 a tick, plus one more every other tick; stopped after three ticks.
  CHECK(animation->pose(1, 1).rotation[1] == 102);
  CHECK(animation->pose(2, 1).rotation[1] == 105);
  CHECK(animation->pose(3, 1).rotation[1] == 107);
  CHECK(animation->pose(12, 1).rotation[1] == 107);
  CHECK(animation->pose(3, 1).location[0] == -6);
  CHECK(animation->pose(3, 2).scale[0] == 2048 + (3 * 2048));
  CHECK(animation->pose(12, 2).scale[0] == 8192);

  // Root motion: 10 units on Z for three ticks; bone 0 itself does not move.
  CHECK(animation->rootMotion[3][2] == doctest::Approx(30.0));
  CHECK(animation->rootMotion[12][2] == doctest::Approx(30.0));
  CHECK(animation->pose(12, 0).location == std::array<std::int32_t, 3>{});

  // The loop body ran twice, turning bone 3 by 4 each time.
  CHECK(animation->pose(6, 3).rotation[0] == 1024 + 4);
  CHECK(animation->pose(12, 3).rotation[0] == 1024 + 8);
  REQUIRE(animation->loops.size() == 1);
  CHECK(animation->loops[0] == AnimationLoop{.firstTick = 5, .lastTick = 7, .count = 2});

  REQUIRE(animation->sounds.size() == 2);
  CHECK(animation->sounds[0] == SoundEvent{.tick = 1, .bank = 4, .sound = 5});
  CHECK(animation->sounds[1] == SoundEvent{.tick = 11, .bank = 1, .sound = 1});
  REQUIRE(animation->textureMoves.size() == 1);
  CHECK(
      animation->textureMoves[0] ==
      TextureMove{.tick = 1, .srcX = 1, .srcY = 2, .width = 3, .height = 4, .dstX = 5, .dstY = 6});
}

TEST_CASE("an animation whose loop repeats forever stops at its first jump back") {
  const auto bytes = buildSampleAnimationForever();
  const auto animation = simulateAnimation(bytes, 1, SampleBoneCount);
  REQUIRE(animation.has_value());
  CHECK_FALSE(animation->hasScale);
  CHECK(animation->endsInLoop);
  REQUIRE(animation->tickCount() == 4);
  CHECK(animation->pose(0, 1).scale == std::array<std::int32_t, 3>{4096, 4096, 4096});
  CHECK(animation->pose(3, 1).rotation[2] == 6);
  REQUIRE(animation->loops.size() == 1);
  CHECK(animation->loops[0] == AnimationLoop{.firstTick = 2, .lastTick = 3, .count = LoopForever});
}

TEST_CASE("root motion turns with the root and skips X for two animation ids") {
  // Root: turn a quarter on Y in one tick, then move 8 a tick along its local X for two ticks.
  const auto bytes = stream(
      {0x0001, 0x8000 | 0x400 | 0, 1, 1024, 0x0002, 0x8000 | 0x400 | 0x100 | 0, 1, 0, 8, 0x0FFF});
  const auto turned = simulateAnimation(bytes, 0, SampleBoneCount);
  REQUIRE(turned.has_value());
  CHECK(turned->pose(2, 0).rotation[1] == 1024);
  // Y by 90 degrees takes local X to -Z.
  CHECK(std::abs(turned->rootMotion[3][0]) < 1e-9);
  CHECK(turned->rootMotion[3][2] == doctest::Approx(-16.0));

  const auto ignored = simulateAnimation(bytes, 0x23, SampleBoneCount);
  REQUIRE(ignored.has_value());
  CHECK(ignored->rootMotion[3][2] == doctest::Approx(0.0));
}

TEST_CASE("a malformed animation is an error, never an endless simulation") {
  // The original runs the first instruction on frame 1 whatever frame it names.
  CHECK(simulateAnimation(stream({0x0001, 0x0FFF}), 0, SampleBoneCount).has_value());

  // A keyframe for bone 4 of 4.
  CHECK_FALSE(
      simulateAnimation(stream({0x0001, 0x8000 | 0x400 | 4, 1, 1, 0x0FFF}), 0, SampleBoneCount)
          .has_value());
  // A divisor of 0.
  CHECK_FALSE(
      simulateAnimation(stream({0x0001, 0x8000 | 0x400 | 1, 0, 1, 0x0FFF}), 0, SampleBoneCount)
          .has_value());
  // An opcode the original does not have (5 would be taken for a loop start: it has bit 12).
  CHECK_FALSE(simulateAnimation(stream({0x6001, 0x0FFF}), 0, SampleBoneCount).has_value());
  // A loop end without a loop start.
  CHECK_FALSE(simulateAnimation(stream({0x2001, 1, 0x0FFF}), 0, SampleBoneCount).has_value());
  // The stream ends inside the initial pose, inside an instruction, and before the word that
  // follows one.
  const auto empty = stream({});
  CHECK_FALSE(simulateAnimation(std::span(empty).first(10), 0, SampleBoneCount).has_value());
  CHECK_FALSE(
      simulateAnimation(stream({0x0001, 0x8000 | 0x400 | 1, 1}), 0, SampleBoneCount).has_value());
  CHECK_FALSE(simulateAnimation(stream({0x4001, 0x0101}), 0, SampleBoneCount).has_value());
  // No bones, or more than a skeleton can have.
  CHECK_FALSE(simulateAnimation(stream({0x0001, 0x0FFF}), 0, 0).has_value());
  CHECK_FALSE(simulateAnimation(stream({0x0001, 0x0FFF}), 0, MaxAnimationBones + 1).has_value());

  // A frame count of 0 is never reached before the frame counter wraps: the tick limit stops it.
  ByteWriter endless;
  endless.u16(0);
  for (std::size_t bone = 1; bone < SampleBoneCount; ++bone) {
    endless.words({0, 0, 0, 0, 0, 0});
  }
  endless.words({0x0001, 0x0FFF});
  CHECK_FALSE(simulateAnimation(endless.bytes(), 0, SampleBoneCount).has_value());

  // A loop that jumps back to its own end in the same tick, 254 times, then once more forever.
  CHECK(simulateAnimation(stream({0x10FE, 0x2001, 1, 0x0FFF}), 0, SampleBoneCount).has_value());
  const auto forever = simulateAnimation(stream({0x10FF, 0x2001, 1, 0x0FFF}), 0, SampleBoneCount);
  REQUIRE(forever.has_value());
  CHECK(forever->endsInLoop);
  CHECK(forever->tickCount() == 1);
}

TEST_CASE("a skeleton lists each parent before its children and draws objects the model has") {
  const std::vector<SkeletonBone> good{{.objIndex = -1, .parentIndex = -1},
                                       {.objIndex = -1, .parentIndex = 0},
                                       {.objIndex = 0, .parentIndex = 1},
                                       {.objIndex = 1, .parentIndex = 2}};
  CHECK(validateSkeleton(good, 2).has_value());
  CHECK_FALSE(validateSkeleton(good, 1).has_value());
  CHECK_FALSE(validateSkeleton({}, 2).has_value());

  auto childFirst = good;
  childFirst[1].parentIndex = 2;
  CHECK_FALSE(validateSkeleton(childFirst, 2).has_value());
  auto ownParent = good;
  ownParent[2].parentIndex = 2;
  CHECK_FALSE(validateSkeleton(ownParent, 2).has_value());
  auto negative = good;
  negative[3].objIndex = -2;
  CHECK_FALSE(validateSkeleton(negative, 2).has_value());
}
