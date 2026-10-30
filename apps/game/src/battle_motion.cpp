#include "battle_motion.hpp"

#include <dwe/animation/clip_sampling.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace dwe::app {

namespace {

// The original's angles (4096 per turn) and fixed-point ones (4096 for 1).
constexpr double Turn = 4096.0;

} // namespace

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

game::battle::BonePose originalPose(const assets::NodeTransform& node) {
  const auto unit = [](double value) { return static_cast<std::int32_t>(std::lround(value)); };
  // The model's rotation is the original's Rx·Ry·Rz turned like the axes (formats: gltf.cpp,
  // toGltfRotation): the original's quaternion has y and z the other way.
  const double w = node.rotation.w;
  const double x = node.rotation.x;
  const double y = -static_cast<double>(node.rotation.y);
  const double z = -static_cast<double>(node.rotation.z);
  // The terms of its matrix that RotMatrix's three angles come out of.
  const double m00 = 1.0 - (2.0 * ((y * y) + (z * z)));
  const double m01 = 2.0 * ((x * y) - (w * z));
  const double m02 = 2.0 * ((x * z) + (w * y));
  const double m12 = 2.0 * ((y * z) - (w * x));
  const double m22 = 1.0 - (2.0 * ((x * x) + (y * y)));
  const auto angle = [&](double radians) {
    return unit(radians / (2.0 * std::numbers::pi) * Turn);
  };
  return {.rotation = {.x = angle(std::atan2(-m12, m22)),
                       .y = angle(std::asin(std::clamp(m02, -1.0, 1.0))),
                       .z = angle(std::atan2(-m01, m00))},
          .location = {.x = unit(node.translation.x),
                       .y = unit(-node.translation.y),
                       .z = unit(-node.translation.z)},
          .scale = {.x = unit(node.scale.x * Turn),
                    .y = unit(node.scale.y * Turn),
                    .z = unit(node.scale.z * Turn)}};
}

std::optional<std::vector<game::battle::BonePose>>
boneChain(const assets::ModelData& model, std::span<const assets::NodeTransform> pose,
          std::uint32_t joint) {
  if (!model.skin || model.skin->joints.empty() || pose.size() != model.nodes.size()) {
    return std::nullopt;
  }
  std::vector<game::battle::BonePose> chain;
  std::optional<std::uint32_t> node =
      model.skin->joints[joint < model.skin->joints.size() ? joint : 0];
  while (node) {
    chain.push_back(originalPose(pose[*node]));
    node = model.nodes[*node].parent;
  }
  std::ranges::reverse(chain);
  return chain;
}

std::optional<std::vector<game::battle::BonePose>> clipBoneChain(const assets::ModelData& model,
                                                                 std::uint32_t clip,
                                                                 std::int32_t frame,
                                                                 std::uint32_t joint) {
  const assets::AnimationClip* found = model.findClip(clip);
  if (found == nullptr || found->ticksPerSecond == 0) {
    return std::nullopt;
  }
  std::vector<assets::NodeTransform> pose(model.nodes.size());
  animation::sampleClip(model, *found, static_cast<double>(frame) / found->ticksPerSecond, pose);
  return boneChain(model, pose, joint);
}

} // namespace dwe::app
