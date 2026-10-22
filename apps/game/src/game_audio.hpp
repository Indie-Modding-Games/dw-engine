#pragma once

#include <dwe/audio/audio.hpp>

namespace dwe::app {

// The game's audio on the default sound device, or on the null output, with a warning, when there
// is none (a machine without sound, the routes' virtual display): the game plays the same either
// way.
[[nodiscard]] audio::Audio openGameAudio();

} // namespace dwe::app
