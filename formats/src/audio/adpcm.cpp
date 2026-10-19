#include <dwe/formats/audio/adpcm.hpp>

#include <algorithm>
#include <array>
#include <limits>

namespace dwe::formats::audio {

namespace {

// The prediction weights of each filter, in 64ths.
constexpr std::array<std::array<int, 2>, 5> FilterWeights = {{{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}}};
constexpr unsigned MaxShift = 12;
constexpr unsigned ReservedShiftValue = 9;
constexpr unsigned MaxFilter = 4;
constexpr std::uint8_t ReservedHeaderBit = 0x80;

} // namespace

DecodedSample decodeAdpcm(std::span<const std::byte> blocks) {
    DecodedSample sample;
    const std::size_t blockCount = blocks.size() / AdpcmBlockSize;
    sample.pcm.reserve(blockCount * AdpcmSamplesPerBlock);
    std::optional<std::size_t> loopStartBlock;
    int previous1 = 0;
    int previous2 = 0;
    for (std::size_t index = 0; index < blockCount; ++index) {
        const auto block = blocks.subspan(index * AdpcmBlockSize, AdpcmBlockSize);
        const auto header = std::to_integer<std::uint8_t>(block[0]);
        const auto flags = std::to_integer<std::uint8_t>(block[1]);
        const unsigned shiftField = header & 0x0FU;
        const unsigned filterField = (header >> 4U) & 0x07U;
        if (shiftField > MaxShift || filterField > MaxFilter || (header & ReservedHeaderBit) != 0) {
            ++sample.reservedBlocks;
        }
        const unsigned shift = shiftField > MaxShift ? ReservedShiftValue : shiftField;
        const auto& weights = FilterWeights[std::min(filterField, MaxFilter)];
        if ((flags & AdpcmFlags::LoopStart) != 0) {
            loopStartBlock = index;
        }
        for (std::size_t i = 0; i < AdpcmSamplesPerBlock; ++i) {
            const auto packed = std::to_integer<unsigned>(block[2 + (i / 2)]);
            const unsigned nibble = (i % 2 == 0 ? packed : packed >> 4U) & 0x0FU;
            // The nibble is the top of a 16-bit value: the cast sign-extends it.
            const int residual = static_cast<std::int16_t>(nibble << 12U) >> shift;
            const int predicted = residual + (((previous1 * weights[0]) + (previous2 * weights[1]) + 32) >> 6);
            const int clamped = std::clamp<int>(predicted, std::numeric_limits<std::int16_t>::min(),
                                                std::numeric_limits<std::int16_t>::max());
            sample.pcm.push_back(static_cast<std::int16_t>(clamped));
            previous2 = previous1;
            previous1 = clamped;
        }
        ++sample.blocks;
        if ((flags & AdpcmFlags::End) != 0) {
            sample.terminated = true;
            if ((flags & AdpcmFlags::Repeat) != 0) {
                sample.loopStartImplied = !loopStartBlock;
                sample.loop = SampleLoop{.start = loopStartBlock.value_or(0) * AdpcmSamplesPerBlock,
                                         .end = (index + 1) * AdpcmSamplesPerBlock};
            }
            return sample;
        }
    }
    sample.trailingBytes = blocks.size() % AdpcmBlockSize;
    return sample;
}

} // namespace dwe::formats::audio
