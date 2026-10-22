#pragma once

#include <dwe/assets/model_data.hpp>
#include <dwe/core/math.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace dwe::animation {

// Where a moment of a clip falls in its keyframes. Past the end of a clip that repeats forever,
// time goes round its repeat; in the step from the repeat's end back to its start the pose blends
// the two.
struct ClipPosition {
  double seconds = 0.0; // the keyframe time to sample
  // When blend > 0, the keyframe time to blend towards (the repeat's start) and how far.
  double towards = 0.0;
  float blend = 0.0F;
  // How many times the repeat has gone round.
  std::uint64_t repeats = 0;
};

// Places a time since the clip started (seconds, from 0) in the clip. A clip that does not repeat
// holds its last pose.
ClipPosition locate(const assets::AnimationClip& clip, double seconds);

// The pose of every node of the model at a time of the clip: the rest pose, with the clip's
// channels applied. The root motion node keeps its rest translation; its movement is rootMotion's.
// `pose` has one entry per node.
void sampleClip(const assets::ModelData& model, const assets::AnimationClip& clip, double seconds,
                std::span<assets::NodeTransform> pose);

// How far the clip's root motion has moved the entity after this time, counting every repeat, in
// the model's space as if the entity faced its rest direction. Zero for a clip without root motion.
glm::vec3 rootMotion(const assets::ModelData& model, const assets::AnimationClip& clip,
                     double seconds);

// The distance a repeat covers divided by its length: the ground speed, in units per second, at
// which a walk or run cycle's feet do not slide. Zero for a clip that does not repeat or has no
// root motion.
float repeatGroundSpeed(const assets::ModelData& model, const assets::AnimationClip& clip);

// The rest pose of every node.
void restPose(const assets::ModelData& model, std::span<assets::NodeTransform> pose);

// Mixes two poses node by node: `t` = 0 gives `from`, 1 gives `to`. Rotations take the short way.
void blendPoses(std::span<const assets::NodeTransform> from,
                std::span<const assets::NodeTransform> to, float t,
                std::span<assets::NodeTransform> out);

// The skinning matrices of a pose: each joint's model-space transform times its inverse bind
// matrix, one per joint of the skin (one identity matrix for a model without a skin). `out` has
// jointCount(model) entries; `nodeWorld`, one per node, is scratch for the nodes' model-space
// transforms, so that a player computing every frame allocates nothing.
void computeJointMatrices(const assets::ModelData& model,
                          std::span<const assets::NodeTransform> pose,
                          std::span<glm::mat4> nodeWorld, std::span<glm::mat4> out);

// How many joint matrices a model needs.
std::size_t jointCount(const assets::ModelData& model);

} // namespace dwe::animation
