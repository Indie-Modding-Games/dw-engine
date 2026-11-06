#pragma once

#include <dwe/core/error.hpp>
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

// Writes a RIFF WAVE file: PCM, one channel, 16 bits. A loop goes into a `smpl` chunk as one
// forward loop that repeats forever, with the last sample of the loop as its end (the chunk's end
// is inclusive).
std::vector<std::byte> writeWav(const WavSound& sound);

// A WAV as readWav reads it.
struct WavFile {
  std::uint32_t sampleRate = 0;
  std::uint16_t channels = 0;
  std::vector<std::int16_t> pcm;  // interleaved, channels samples per frame
  std::optional<SampleLoop> loop; // in frames, the end excluded
  std::uint8_t unityNote = 60;
};

// The highest sample rate readWav takes.
inline constexpr std::uint32_t MaxWavSampleRate = 384000;

// Reads a RIFF WAVE file of 16-bit PCM with one or two channels, and the first loop of its smpl
// chunk if it has one. Chunks it does not need are skipped. Fails, with the reason, on another
// encoding or a file that does not hold together.
core::Result<WavFile> readWav(std::span<const std::byte> bytes);

} // namespace dwe::formats::audio
