#include "battle_motion.hpp"

#include <dwe/animation/clip_sampling.hpp>

#include <cmath>
#include <cstddef>
#include <vector>

namespace dwe::app {

game::battle::FighterClips battleClipsOf(const assets::ModelData& model) {
  game::battle::FighterClips out;
  for (const assets::AnimationClip& clip : model.clips) {
    if (!clip.id || clip.ticksPerSecond == 0 || clip.tickCount == 0) {
      continue;
    }
    const auto tick = static_cast<double>(clip.ticksPerSecond);
    game::battle::ClipMotion motion;
    glm::vec3 before{0.0F};
    for (std::uint32_t frame = 0; frame < clip.tickCount; ++frame) {
      const glm::vec3 after = animation::rootMotion(model, clip, frame / tick);
      // The model faces its +z; its x is the battle's side axis.
      motion.steps.push_back(
          {.side = static_cast<std::int32_t>(std::lround(after.x - before.x)),
           .forward = static_cast<std::int32_t>(std::lround(after.z - before.z))});
      before = after;
    }
    if (clip.repeat) {
      motion.repeatFrom = static_cast<std::size_t>(std::lround(clip.repeat->start * tick));
    }
    out.emplace(static_cast<std::int32_t>(*clip.id), std::move(motion));
  }
  return out;
}

std::optional<glm::mat4> clipJointAt(const assets::ModelData& model, std::uint32_t clip,
                                     std::int32_t frame, std::uint32_t joint) {
  const assets::AnimationClip* found = model.findClip(clip);
  if (found == nullptr || !model.skin || model.skin->joints.empty() || found->ticksPerSecond == 0) {
    return std::nullopt;
  }
  std::vector<assets::NodeTransform> pose(model.nodes.size());
  animation::sampleClip(model, *found, static_cast<double>(frame) / found->ticksPerSecond, pose);
  std::vector<glm::mat4> nodeWorld(model.nodes.size());
  std::vector<glm::mat4> joints(animation::jointCount(model));
  animation::computeJointMatrices(model, pose, nodeWorld, joints);
  const std::size_t index = joint < model.skin->joints.size() ? joint : 0;
  return nodeWorld.at(model.skin->joints[index]);
}

} // namespace dwe::app
