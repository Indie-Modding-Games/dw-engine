#pragma once

#include <dwe/audio/clip.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace dwe::audio::tests {

// A clip whose samples are given frame by frame (and channel by channel).
inline std::shared_ptr<const Clip> clipOf(std::vector<std::int16_t> pcm, std::uint16_t channels = 1,
                                          std::uint32_t sampleRate = 48000,
                                          std::optional<ClipLoop> loop = std::nullopt) {
  return std::make_shared<const Clip>(
      Clip{.sampleRate = sampleRate, .channels = channels, .pcm = std::move(pcm), .loop = loop});
}

// A mono clip that holds one value for frames frames.
inline std::shared_ptr<const Clip> steadyClip(std::int16_t value, std::size_t frames,
                                              std::optional<ClipLoop> loop = std::nullopt) {
  return clipOf(std::vector<std::int16_t>(frames, value), 1, 48000, loop);
}

} // namespace dwe::audio::tests
