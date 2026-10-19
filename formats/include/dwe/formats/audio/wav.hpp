#pragma once

#include <dwe/formats/audio/adpcm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace dwe::formats::audio {

// A mono 16-bit sound to write as a WAV.
struct WavSound {
    std::span<const std::int16_t> pcm;
    std::uint32_t sampleRate = 44100;
    std::optional<SampleLoop> loop;
    std::uint8_t unityNote = 60; // the key that plays the sound at its own rate, for samplers
};

// Writes a RIFF WAVE file: PCM, one channel, 16 bits. A loop goes into a `smpl` chunk as one forward loop that repeats
// forever, with the last sample of the loop as its end (the chunk's end is inclusive).
std::vector<std::byte> writeWav(const WavSound& sound);

} // namespace dwe::formats::audio
