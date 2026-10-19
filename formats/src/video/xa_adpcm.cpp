#include <dwe/formats/video/xa_adpcm.hpp>

#include <algorithm>
#include <format>

namespace dwe::formats::video {

namespace {

constexpr std::uint8_t CodingStereo = 0x01;
constexpr std::uint8_t CodingHalfRate = 0x04;
constexpr std::uint8_t CodingEightBit = 0x10;
constexpr std::uint8_t CodingEmphasis = 0x40;
constexpr std::uint8_t CodingReserved = 0xAA;
constexpr std::size_t GroupHeaderSize = 16;
constexpr std::size_t MaxUnits = 8;

// The prediction filters, in 64ths: weight of the previous sample and of the one before it.
constexpr std::array<std::array<std::int32_t, 2>, 4> Filters = {{{0, 0}, {60, 0}, {115, -52}, {98, -55}}};

// Shifts 13 to 15 are reserved and act as 9.
unsigned shiftOf(std::uint8_t parameter) {
    const unsigned shift = parameter & 0x0FU;
    return shift > 12 ? 9 : shift;
}

std::uint8_t byteAt(std::span<const std::byte> bytes, std::size_t offset) {
    return std::to_integer<std::uint8_t>(bytes[offset]);
}

} // namespace

core::Result<XaCoding> readXaCoding(std::uint8_t coding) {
    if ((coding & CodingReserved) != 0) {
        return core::fail(std::format("the XA coding {:#04x} uses reserved bits", coding));
    }
    if ((coding & CodingEmphasis) != 0) {
        return core::fail(std::format("the XA coding {:#04x} asks for emphasis, which is not supported", coding));
    }
    return XaCoding{.channels = static_cast<std::uint8_t>((coding & CodingStereo) != 0 ? 2 : 1),
                    .sampleRate = (coding & CodingHalfRate) != 0 ? 18900U : 37800U,
                    .bitsPerSample = static_cast<std::uint8_t>((coding & CodingEightBit) != 0 ? 8 : 4)};
}

std::size_t xaSampleFramesPerSector(const XaCoding& coding) {
    const std::size_t units = coding.bitsPerSample == 4 ? 8 : 4;
    return XaSoundGroups * units * XaSamplesPerUnit / coding.channels;
}

core::Result<std::size_t> XaDecoder::decodeSector(std::span<const std::byte> data, const XaCoding& coding,
                                                  std::vector<std::int16_t>& out) {
    if (data.size() < XaSoundGroups * XaSoundGroupSize) {
        return core::fail(std::format("an XA sector of {} bytes is too short for 18 sound groups", data.size()));
    }
    if ((coding.channels != 1 && coding.channels != 2) || (coding.bitsPerSample != 4 && coding.bitsPerSample != 8)) {
        return core::fail("unsupported XA coding");
    }
    const bool fourBit = coding.bitsPerSample == 4;
    const bool stereo = coding.channels == 2;
    const std::size_t units = fourBit ? 8 : 4;
    std::size_t mismatches = 0;
    std::array<std::array<std::int16_t, XaSamplesPerUnit>, MaxUnits> samples{};
    for (std::size_t group = 0; group < XaSoundGroups; ++group) {
        const auto bytes = data.subspan(group * XaSoundGroupSize, XaSoundGroupSize);
        for (std::size_t unit = 0; unit < units; ++unit) {
            // Parameters at bytes 4..11; bytes 0..3 copy 4..7 and bytes 12..15 copy 8..11.
            const std::uint8_t parameter = byteAt(bytes, 4 + unit);
            if (parameter != byteAt(bytes, unit < 4 ? unit : unit + 8)) {
                ++mismatches;
            }
            const unsigned shift = shiftOf(parameter);
            const auto& filter = Filters[(parameter >> 4U) & 0x03U];
            auto& history = history_[stereo ? unit % 2 : 0];
            for (std::size_t sample = 0; sample < XaSamplesPerUnit; ++sample) {
                std::int32_t expanded = 0;
                if (fourBit) {
                    const std::uint8_t packed = byteAt(bytes, GroupHeaderSize + (sample * 4) + (unit / 2));
                    const unsigned nibble = unit % 2 == 0 ? packed & 0x0FU : packed >> 4U;
                    expanded = (static_cast<std::int32_t>(nibble ^ 0x08U) - 8) * 4096;
                } else {
                    expanded = static_cast<std::int8_t>(byteAt(bytes, GroupHeaderSize + (sample * 4) + unit)) * 256;
                }
                const std::int32_t predicted = ((history[0] * filter[0]) + (history[1] * filter[1]) + 32) >> 6;
                const std::int32_t value = std::clamp((expanded >> shift) + predicted, -32768, 32767);
                history[1] = history[0];
                history[0] = value;
                samples[unit][sample] = static_cast<std::int16_t>(value);
            }
        }
        if (stereo) {
            for (std::size_t pair = 0; pair < units / 2; ++pair) {
                for (std::size_t sample = 0; sample < XaSamplesPerUnit; ++sample) {
                    out.push_back(samples[pair * 2][sample]);
                    out.push_back(samples[(pair * 2) + 1][sample]);
                }
            }
        } else {
            for (std::size_t unit = 0; unit < units; ++unit) {
                out.insert(out.end(), samples[unit].begin(), samples[unit].end());
            }
        }
    }
    return mismatches;
}

} // namespace dwe::formats::video
