#pragma once

#include <dwe/audio/mixing.hpp>
#include <dwe/core/error.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace dwe::audio {

// The part of a clip that repeats until the voice is stopped: frames [start, start + length).
struct ClipLoop {
  std::uint64_t start = 0;
  std::uint64_t length = 0;
};

// A sound ready to play: 16-bit PCM, mono or stereo, at any rate.
struct Clip {
  std::uint32_t sampleRate = 0;
  std::uint16_t channels = 0;
  std::vector<std::int16_t> pcm; // interleaved, channels samples per frame
  std::optional<ClipLoop> loop;

  [[nodiscard]] std::uint64_t frameCount() const;
};

// Whether the mixer can play a clip: one or two channels, a rate up to MaxClipSampleRate, whole
// frames, at least one frame, and a non-empty loop inside the clip. Fails with the reason.
core::Result<void> checkClip(const Clip& clip);

} // namespace dwe::audio
