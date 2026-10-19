#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace dwe::assets {

// The format of a WAV file's samples.
struct WavInfo {
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;
    std::uint64_t frames = 0;
};

// Reads the header of a WAV file: RIFF/WAVE with a PCM or float `fmt ` chunk and a `data` chunk inside the file.
core::Result<WavInfo> readWavHeader(std::span<const std::byte> wav);

// What a SoundFont holds.
struct SoundFontInfo {
    std::size_t presets = 0; // without the terminal record
    std::size_t samples = 0; // without the terminal record
};

// Reads the structure of a SoundFont 2: RIFF/sfbk with its INFO, sdta and pdta lists, and every record table of
// pdta present and made of whole records.
core::Result<SoundFontInfo> readSoundFontHeader(std::span<const std::byte> sf2);

// The layout of a standard MIDI file.
struct MidiInfo {
    std::uint16_t format = 0;
    std::uint16_t tracks = 0;
    std::uint16_t division = 0;
};

// Reads the header of a standard MIDI file and checks that its tracks are inside the file.
core::Result<MidiInfo> readMidiHeader(std::span<const std::byte> midi);

// The picture size of a video.
struct VideoInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// Reads the header line of a YUV4MPEG2 (.y4m) video.
core::Result<VideoInfo> readY4mHeader(std::span<const std::byte> y4m);

} // namespace dwe::assets
