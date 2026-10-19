#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "cook.hpp"

namespace dwe::dwx {

// The Opus settings of the audio cooker. Fixed, so the same WAV always cooks to the same bytes.
inline constexpr std::int32_t OpusBitratePerChannel = 64000;
inline constexpr int OpusComplexity = 10;
// The Ogg serial number of every stream: libopusenc would pick a random one.
inline constexpr std::int32_t OpusSerial = 0x64776531; // "dwe1"

// Where a loop of a sound at rate falls once it is resampled to 48 kHz: rounded to the nearest frame and kept inside
// the frames the decoder gives, ceil(frames * 48000 / rate).
struct OpusLoop {
    std::uint64_t start = 0;
    std::uint64_t length = 0;

    friend bool operator==(const OpusLoop&, const OpusLoop&) = default;
};
OpusLoop opusLoopOf(std::uint64_t start, std::uint64_t end, std::uint64_t frames, std::uint32_t rate);

// Cooks a 16-bit PCM WAV into Ogg Opus with libopusenc, which resamples it to 48 kHz. Its loop goes into the LOOPSTART
// and LOOPLENGTH comments and its unity note into DWE_UNITY_NOTE, in frames of the decoded sound, which
// assets::decodeOpus reads back. The stream has a fixed serial number and no comment padding.
core::Result<std::vector<std::byte>> cookAudio(std::span<const std::byte> source, const CookOptions& options);

} // namespace dwe::dwx
