#pragma once

#include <span>

struct tsf;

namespace dwe::audio {

// Renders the music synthesizer's voices for as many frames as dry holds (interleaved stereo):
// every voice into dry, and each voice whose zone has a reverb send also into wet, scaled by it.
// scratch holds one voice and is at least as long as dry; wet is as long as dry.
void renderSynth(tsf* synth, std::span<float> dry, std::span<float> wet,
                 std::span<float> scratch) noexcept;

} // namespace dwe::audio
