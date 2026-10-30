#pragma once

#include <dwe/assets/model_data.hpp>
#include <dwe/game/battle/motion.hpp>

#include <glm/mat4x4.hpp>

#include <cstdint>
#include <optional>

namespace dwe::app {

// The clips of a model as the battle plays them (anim.c: tickAnimation, applyRootMomentum): each
// clip with an original id and sampling, frame by frame, with the step its root takes on each frame
// in the model's axes. The extracted clips keep the root's motion in its own channel, one key a
// frame, as the original added it up.
game::battle::FighterClips battleClipsOf(const assets::ModelData& model);

// Joint `joint` of the model (the original's object index; the first for one the skin lacks) in
// the model's space on frame `frame` of the clip with original id `clip`; nothing without a skin or
// that clip.
std::optional<glm::mat4> clipJointAt(const assets::ModelData& model, std::uint32_t clip,
                                     std::int32_t frame, std::uint32_t joint);

} // namespace dwe::app
