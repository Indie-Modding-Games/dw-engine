#pragma once

// Synthetic video fixtures built by the tests: hand-written bitstreams and STR sectors. Nothing here comes from a
// real disc.

#include <dwe/formats/disc/sector.hpp>
#include <dwe/formats/video/str.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "disc_fixtures.hpp"

namespace fixtures {

// Writes bits as libpress reads them: most significant first, in 16-bit little-endian words.
class BitWriter {
public:
    // Appends bits written as '0' and '1'; spaces are ignored.
    void bits(std::string_view text) {
        for (const char c : text) {
            if (c == '0' || c == '1') {
                bit(c == '1');
            }
        }
    }

    // Appends the low count bits of value, most significant first.
    void value(std::uint32_t number, unsigned count) {
        for (unsigned i = count; i > 0; --i) {
            bit(((number >> (i - 1)) & 1U) != 0);
        }
    }

    // Returns the words, the last one padded with zeros.
    [[nodiscard]] std::vector<std::byte> bytes() const {
        std::vector<std::byte> out;
        for (const std::uint16_t word : words_) {
            out.push_back(static_cast<std::byte>(word & 0xFFU));
            out.push_back(static_cast<std::byte>(word >> 8U));
        }
        return out;
    }

private:
    void bit(bool set) {
        if (count_ % 16 == 0) {
            words_.push_back(0);
        }
        if (set) {
            words_.back() = static_cast<std::uint16_t>(words_.back() | (1U << (15 - (count_ % 16))));
        }
        ++count_;
    }

    std::vector<std::uint16_t> words_;
    std::size_t count_ = 0;
};

// The header's count for a frame that gives codeCount MDEC codes.
inline std::uint16_t halfCodeCount(std::size_t codeCount) {
    return static_cast<std::uint16_t>((((codeCount + 1) / 2) + 31) & ~std::size_t{31});
}

// A frame's bitstream: the 8-byte header, then the body.
inline std::vector<std::byte> makeBitstream(std::uint16_t version, std::uint16_t scale, std::size_t codeCount,
                                            const BitWriter& body) {
    std::vector<std::byte> out(8);
    putU16(out, 0, halfCodeCount(codeCount));
    putU16(out, 2, 0x3800);
    putU16(out, 4, scale);
    putU16(out, 6, version);
    const auto rest = body.bytes();
    out.insert(out.end(), rest.begin(), rest.end());
    return out;
}

// The user data of a video sector: StHEADER, then this sector's part of the frame.
struct VideoSectorSpec {
    std::uint16_t sectorInFrame = 0;
    std::uint16_t sectorsInFrame = 0;
    std::uint32_t frameNumber = 0;
    std::uint32_t frameBytes = 0;
    std::uint16_t width = 16;
    std::uint16_t height = 16;
    std::uint32_t headm = 0;
    std::uint32_t headv = 0;
};

inline std::vector<std::byte> makeVideoSectorData(const VideoSectorSpec& spec, std::span<const std::byte> chunk) {
    std::vector<std::byte> data(dwe::formats::disc::Form1DataSize);
    putU16(data, 0x00, 0x0160);
    putU16(data, 0x02, 0x8001);
    putU16(data, 0x04, spec.sectorInFrame);
    putU16(data, 0x06, spec.sectorsInFrame);
    putU32(data, 0x08, spec.frameNumber);
    putU32(data, 0x0C, spec.frameBytes);
    putU16(data, 0x10, spec.width);
    putU16(data, 0x12, spec.height);
    putU32(data, 0x14, spec.headm);
    putU32(data, 0x18, spec.headv);
    for (std::size_t i = 0; i < chunk.size() && i < dwe::formats::video::StChunkSize; ++i) {
        data[dwe::formats::video::StHeaderSize + i] = chunk[i];
    }
    return data;
}

// A decoded sector over user data the caller keeps.
inline dwe::formats::disc::SectorData sectorOf(std::span<const std::byte> data, dwe::formats::disc::SectorForm form,
                                               std::uint8_t submode, std::uint8_t coding = 0) {
    return dwe::formats::disc::SectorData{
        .form = form,
        .userData = data,
        .subheader = dwe::formats::disc::XaSubheader{.file = 1, .channel = 1, .submode = submode, .coding = coding}};
}

} // namespace fixtures
