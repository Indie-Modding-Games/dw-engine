#pragma once

#include <dwe/assets/model_data.hpp>
#include <dwe/game/battle/effect_runner.hpp>
#include <dwe/game/battle/motion.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace dwe::app {

// The clips of a model as the battle plays them (anim.c: tickAnimation, applyRootMomentum): each
// clip with an original id and sampling, frame by frame, with the step its root takes on each frame
// in the model's axes. The extracted clips keep the root's motion in its own channel, one key a
// frame, as the original added it up.
game::battle::FighterClips battleClipsOf(const assets::ModelData& model);

// A node's transform as the original poses it (PositionData): the axes turned back (y and z the
// other way), and the rotation as RotMatrix's angles, rounded to the original's units.
game::battle::BonePose originalPose(const assets::NodeTransform& node);

// The nodes from the model's top down to joint `joint` (the original's object index; the first for
// one the skin lacks) in `pose`, each relative to the one before, as the original poses them
// (game::battle::boneMatrix); nothing without a skin.
std::optional<std::vector<game::battle::BonePose>>
boneChain(const assets::ModelData& model, std::span<const assets::NodeTransform> pose,
          std::uint32_t joint);

// boneChain on frame `frame` of the clip with original id `clip`; nothing without a skin or that
// clip.
std::optional<std::vector<game::battle::BonePose>> clipBoneChain(const assets::ModelData& model,
                                                                 std::uint32_t clip,
                                                                 std::int32_t frame,
                                                                 std::uint32_t joint);

} // namespace dwe::app
