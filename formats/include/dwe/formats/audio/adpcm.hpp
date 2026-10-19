#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// The sound chip's ADPCM, the format of every sample in a VB (the sample half of a VAB). A sample is a run of 16-byte
// blocks of 28 samples each: byte 0 holds the shift (low nibble) and the filter (bits 4-6), byte 1 the flags, and
// bytes 2-15 the 4-bit residuals, low nibble first.
namespace dwe::formats::audio {

inline constexpr std::size_t AdpcmBlockSize = 16;
inline constexpr std::size_t AdpcmSamplesPerBlock = 28;

// The flags of a block.
struct AdpcmFlags {
    static constexpr std::uint8_t End = 0x01;       // the voice leaves the block for the loop start
    static constexpr std::uint8_t Repeat = 0x02;    // with End: keep playing from the loop start; without: release
    static constexpr std::uint8_t LoopStart = 0x04; // the voice takes this block as its loop start
};

// A loop in samples: playback goes back to start after the sample before end.
struct SampleLoop {
    std::size_t start = 0;
    std::size_t end = 0;

    bool operator==(const SampleLoop&) const = default;
};

// A decoded sample.
struct DecodedSample {
    std::vector<std::int16_t> pcm;
    std::size_t blocks = 0;         // blocks decoded, the End block included
    bool terminated = false;        // a block with End was found; otherwise the data ran out first
    std::optional<SampleLoop> loop; // when the End block also has Repeat
    bool loopStartImplied = false;  // the End block repeats but no block marks a loop start: the loop starts at 0
    std::size_t reservedBlocks = 0; // blocks with a reserved shift (13-15), filter (5-7) or header bit 7
    std::size_t trailingBytes = 0;  // bytes after the last whole block, when the data ran out
};

// Decodes blocks up to the first one with End, which is included, as the sound chip plays them: residual << 12 >>
// shift plus the prediction of filter f from the last two samples, (s1 * k0[f] + s2 * k1[f] + 32) >> 6, clamped to
// 16 bits. Reserved values decode as the hardware does: a shift above 12 as 9 and a filter above 4 as 4. The loop
// starts at the last block with LoopStart before the end, and ends after the End block.
DecodedSample decodeAdpcm(std::span<const std::byte> blocks);

} // namespace dwe::formats::audio
