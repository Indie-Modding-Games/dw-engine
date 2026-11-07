// The one translation unit that compiles the music synthesizer, a single-header library
// (docs/architecture/audio.md, "Música (tramo 3)").
#define TSF_IMPLEMENTATION
#include <tsf.h>

#include <algorithm>
#include <cstddef>

#include "synth_render.hpp"

namespace dwe::audio {

namespace {

// The SoundFont's full reverb send, in 0.1 %.
constexpr int FullReverbSend = 1000;

} // namespace

// The synthesizer's own loop (tsf_render_float) with each voice rendered on its own, so the voices
// whose zone has a reverb send also reach the chip's reverb.
void renderSynth(tsf* synth, std::span<float> dry, std::span<float> wet,
                 std::span<float> scratch) noexcept {
  const auto frames = static_cast<int>(dry.size() / 2);
  std::ranges::fill(dry, 0.0F);
  std::ranges::fill(wet, 0.0F);
  tsf_voice* const end = synth->voices + synth->voiceNum;
  for (tsf_voice* voice = synth->voices; voice != end; ++voice) {
    if (voice->playingPreset == -1) {
      continue;
    }
    const int send = std::clamp(voice->region->reverbEffectsSend, 0, FullReverbSend);
    if (send == 0) {
      tsf_voice_render(synth, voice, dry.data(), frames);
      continue;
    }
    const auto voiceFrames = scratch.first(dry.size());
    std::ranges::fill(voiceFrames, 0.0F);
    tsf_voice_render(synth, voice, voiceFrames.data(), frames);
    const float gain = static_cast<float>(send) / static_cast<float>(FullReverbSend);
    for (std::size_t i = 0; i < voiceFrames.size(); ++i) {
      dry[i] += voiceFrames[i];
      wet[i] += voiceFrames[i] * gain;
    }
  }
}

} // namespace dwe::audio
