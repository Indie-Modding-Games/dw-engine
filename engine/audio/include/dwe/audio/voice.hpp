#pragma once

#include <dwe/audio/clip.hpp>
#include <dwe/audio/mixing.hpp>

#include <cstdint>
#include <memory>

namespace dwe::audio {

// A voice the game asked for. The generation tells a voice from an earlier one in the same slot,
// so stopping a voice that already ended does nothing to the one that took its place.
struct VoiceId {
  std::uint32_t slot = 0;
  std::uint32_t generation = 0;

  friend bool operator==(const VoiceId&, const VoiceId&) = default;
};

// What a voice plays and how. A clip with a loop repeats it until the voice is stopped.
struct PlayRequest {
  std::shared_ptr<const Clip> clip;
  Bus bus = Bus::Effects;
  // A linear gain, from 0 to 1.
  float gain = 1.0F;
  // From -1 (left) to 1 (right).
  float pan = 0.0F;
  // The playback speed: 2 is an octave up.
  float pitch = 1.0F;
};

} // namespace dwe::audio
