#pragma once

#include <dwe/assets/character_model.hpp>

#include <cstdint>
#include <functional>
#include <map>

namespace dwe::app {

// The clips a partner plays instead of the field animations its model lacks (models/partner.yaml),
// by the missing clip's id. Every partner form of the original has them all; an enemy's or a
// character's model, made the partner with the developer's evolve, may have only a few: a moving
// animation (the walks and the run) plays the walk, or the idle without one, and every other (the
// tired idle and the gestures) the idle. Nothing when the model has no idle either.
[[nodiscard]] std::map<std::uint32_t, std::uint32_t>
partnerClipFallbacks(const assets::CharacterModel& character,
                     const std::function<bool(std::uint32_t)>& hasClip);

} // namespace dwe::app
