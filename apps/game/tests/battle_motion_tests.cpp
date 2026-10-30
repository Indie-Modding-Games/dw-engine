#include <dwe/assets/model_data.hpp>
#include <dwe/game/battle/effect_runner.hpp>
#include <dwe/game/battle/motion.hpp>

#include <doctest/doctest.h>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <optional>
#include <vector>

#include "battle_motion.hpp"

using dwe::assets::AnimationClip;
using dwe::assets::ChannelPath;
using dwe::assets::ModelData;
using dwe::game::battle::RootStep;

namespace {

constexpr float TicksPerSecond = 20.0F;

// A made-up clip with the root's translation keyed once a tick.
AnimationClip rootClip(std::uint32_t id, const std::vector<glm::vec4>& root) {
  AnimationClip clip;
  clip.name = "made_up";
  clip.id = id;
  clip.ticksPerSecond = 20;
  clip.tickCount = static_cast<std::uint32_t>(root.size());
  clip.frameCount = clip.tickCount;
  clip.duration = static_cast<float>(root.size() - 1) / TicksPerSecond;
  clip.rootMotionNode = 0;
  std::vector<float> times;
  for (std::size_t t = 0; t < root.size(); ++t) {
    times.push_back(static_cast<float>(t) / TicksPerSecond);
  }
  clip.channels.push_back(
      {.node = 0, .path = ChannelPath::Translation, .times = times, .values = root});
  return clip;
}

// One root joint. Clip 0x24 walks forward (the model's +z) 10, 30 and 60 units and repeats from its
// second tick; clip 0x28 steps 5 to the side and then back 40 twice.
ModelData madeUpModel() {
  ModelData model;
  model.nodes = {{.name = "bone_00", .parent = std::nullopt, .rest = {}}};
  model.nodeOrder = {0};
  AnimationClip walk = rootClip(0x24, {{0, 0, 10, 0}, {0, 0, 40, 0}, {0, 0, 100, 0}});
  walk.repeat = dwe::assets::ClipRepeat{
      .start = 1 / TicksPerSecond, .end = 2 / TicksPerSecond, .gap = 1 / TicksPerSecond};
  model.clips.push_back(walk);
  model.clips.push_back(rootClip(0x28, {{5, 0, 0, 0}, {5, 0, -40, 0}, {5, 0, -80, 0}}));
  AnimationClip unnamed = rootClip(0, {{0, 0, 0, 0}});
  unnamed.id = std::nullopt;
  model.clips.push_back(unnamed);
  return model;
}

} // namespace

TEST_CASE("a model's clips give the battle each frame's root step, by the clip's original id") {
  const dwe::game::battle::FighterClips clips = dwe::app::battleClipsOf(madeUpModel());
  REQUIRE(clips.size() == 2);
  const auto& walk = clips.at(0x24);
  CHECK(walk.steps == std::vector<RootStep>{{.side = 0, .forward = 10},
                                            {.side = 0, .forward = 30},
                                            {.side = 0, .forward = 60}});
  CHECK(walk.repeatFrom == std::optional<std::size_t>{1});
  const auto& hit = clips.at(0x28);
  CHECK(hit.steps == std::vector<RootStep>{{.side = 5, .forward = 0},
                                           {.side = 0, .forward = -40},
                                           {.side = 0, .forward = -40}});
  CHECK_FALSE(hit.repeatFrom.has_value());
}

namespace {

// A node rotated as the extractor writes it (formats: toGltfRotation): Rx·Ry·Rz of the original's
// angles, with y and z turned like the axes.
glm::quat gltfRotation(int x, int y, int z) {
  const auto about = [](int angle, glm::vec3 axis) {
    return glm::angleAxis(static_cast<float>(angle) * glm::two_pi<float>() / 4096.0F, axis);
  };
  const glm::quat q = about(x, {1, 0, 0}) * about(y, {0, 1, 0}) * about(z, {0, 0, 1});
  return {q.w, q.x, -q.y, -q.z};
}

} // namespace

TEST_CASE("a model's nodes go back to the original's pose for the effects' bone matrices") {
  ModelData model;
  model.nodes = {{.name = "bone_00", .parent = std::nullopt, .rest = {}},
                 {.name = "bone_01",
                  .parent = 0,
                  .rest = {.translation = {0.0F, 110.0F, 50.0F},
                           .rotation = gltfRotation(0, -1024, 0),
                           .scale = glm::vec3{1.0F}}},
                 {.name = "bone_02",
                  .parent = 1,
                  .rest = {.translation = {5.0F, 0.0F, 0.0F},
                           .rotation = gltfRotation(300, -200, -1702),
                           .scale = glm::vec3{0.5F, 1.0F, 1.0F}}}};
  model.nodeOrder = {0, 1, 2};
  model.skin = dwe::assets::ModelSkin{.joints = {0, 1, 2}, .inverseBindMatrices = {}};
  std::vector<dwe::assets::NodeTransform> pose;
  for (const auto& node : model.nodes) {
    pose.push_back(node.rest);
  }
  using dwe::game::battle::BonePose;
  const std::optional<std::vector<BonePose>> chain = dwe::app::boneChain(model, pose, 2);
  REQUIRE(chain);
  REQUIRE(chain->size() == 3);
  CHECK((*chain)[0] == BonePose{});
  CHECK((*chain)[1] == BonePose{.rotation = {.x = 0, .y = -1024, .z = 0},
                                .location = {.x = 0, .y = -110, .z = -50}});
  CHECK((*chain)[2] == BonePose{.rotation = {.x = 300, .y = -200, .z = -1702},
                                .location = {.x = 5, .y = 0, .z = 0},
                                .scale = {.x = 2048, .y = 4096, .z = 4096}});
  // A node the skin lacks stands for its first.
  const std::optional<std::vector<BonePose>> first = dwe::app::boneChain(model, pose, 9);
  REQUIRE(first);
  CHECK(first->size() == 1);
}
