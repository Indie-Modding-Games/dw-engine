#pragma once

#include <dwe/assets/clip_motion.hpp>

namespace dwe::game::battle {

using assets::ClipMotion;
using assets::RootStep;

// Every clip of a fighter's model, by the original's animation id (the ids ShownClip uses).
using FighterClips = assets::ClipMotions;

} // namespace dwe::game::battle
