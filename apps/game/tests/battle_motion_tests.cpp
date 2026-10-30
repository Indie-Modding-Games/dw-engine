#include <dwe/assets/model_data.hpp>
#include <dwe/game/battle/motion.hpp>

#include <doctest/doctest.h>

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
