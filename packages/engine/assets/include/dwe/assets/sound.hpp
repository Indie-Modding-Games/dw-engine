#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace dwe::assets {

// Opus always decodes at this rate, whatever the rate of the sound it was made from.
inline constexpr std::uint32_t OpusSampleRate = 48000;
// The longest sound decodeOpus accepts, in frames: an hour. A forged length cannot ask for more
// memory than this.
inline constexpr std::uint64_t MaxSoundFrames = std::uint64_t{OpusSampleRate} * 60 * 60;

// A part of a sound that repeats: frames [start, start + length).
struct SoundLoop {
  std::uint64_t start = 0;
  std::uint64_t length = 0;

  friend bool operator==(const SoundLoop&, const SoundLoop&) = default;
};

// A decoded sound: 16-bit PCM.
struct Sound {
  std::uint32_t sampleRate = OpusSampleRate;
  std::uint16_t channels = 0;
  std::vector<std::int16_t> pcm; // interleaved, channels samples per frame
  std::optional<SoundLoop> loop;
  // The MIDI key that plays the sound at its own pitch, for samplers.
  std::uint8_t unityNote = 60;
  // The rate of the sound it was encoded from, as its header says; a sound bank pitches by it.
  std::uint32_t sourceRate = 0;
};

// Decodes an Ogg Opus file (RFC 7845) with one stream of one or two channels, as dwx cook writes
// it: the decoded frames are trimmed by the header's pre-skip and the last page's granule position.
// The loop comes from the LOOPSTART and LOOPLENGTH comments and the unity note from DWE_UNITY_NOTE,
// all in decoded frames. Fails, with the reason, on a damaged file or one it does not take.
core::Result<Sound> decodeOpus(std::span<const std::byte> file);

// Decodes a WAV file of 16-bit PCM in one or two channels, as dwx writes its sounds. The loop and
// the unity note come from the smpl chunk, if there is one. Fails, with the reason, on a damaged
// file or one it does not take.
core::Result<Sound> decodeWav(std::span<const std::byte> file);

} // namespace dwe::assets
