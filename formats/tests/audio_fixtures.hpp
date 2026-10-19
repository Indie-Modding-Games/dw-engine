#pragma once

#include <dwe/formats/audio/adpcm.hpp>
#include <dwe/formats/audio/vab.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Builders of made-up sound data for the tests: ADPCM blocks, VABs, VHBs and SEQs. Nothing here comes from a disc.
namespace dwe::formats::audio::test {

// Appends little-endian (or big-endian, for SEQ headers) fields.
class Bytes {
public:
    Bytes& u8(std::uint32_t value) {
        bytes_.push_back(static_cast<std::byte>(value & 0xFFU));
        return *this;
    }
    Bytes& u16(std::uint32_t value) { return u8(value).u8(value >> 8U); }
    Bytes& u32(std::uint32_t value) { return u16(value).u16(value >> 16U); }
    Bytes& be16(std::uint32_t value) { return u8(value >> 8U).u8(value); }
    Bytes& be24(std::uint32_t value) { return u8(value >> 16U).be16(value); }
    Bytes& be32(std::uint32_t value) { return be16(value >> 16U).be16(value); }
    Bytes& text(std::string_view text) {
        for (const char c : text) {
            u8(static_cast<unsigned char>(c));
        }
        return *this;
    }
    Bytes& fill(std::size_t count, std::uint8_t value = 0) {
        bytes_.insert(bytes_.end(), count, std::byte{value});
        return *this;
    }
    Bytes& append(std::span<const std::byte> other) {
        bytes_.insert(bytes_.end(), other.begin(), other.end());
        return *this;
    }
    void put32(std::size_t offset, std::uint32_t value) {
        for (std::size_t i = 0; i < 4; ++i) {
            bytes_[offset + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
        }
    }
    [[nodiscard]] std::size_t size() const { return bytes_.size(); }
    [[nodiscard]] const std::vector<std::byte>& bytes() const { return bytes_; }

private:
    std::vector<std::byte> bytes_;
};

// One ADPCM block: header (filter << 4 | shift), flags, then 28 nibbles.
inline std::array<std::byte, AdpcmBlockSize> adpcmBlock(std::uint8_t filter, std::uint8_t shift, std::uint8_t flags,
                                                        const std::array<std::uint8_t, AdpcmSamplesPerBlock>& nibbles) {
    std::array<std::byte, AdpcmBlockSize> block{};
    block[0] = static_cast<std::byte>(((filter & 0x0FU) << 4U) | (shift & 0x0FU));
    block[1] = static_cast<std::byte>(flags);
    for (std::size_t i = 0; i < AdpcmSamplesPerBlock; i += 2) {
        block[2 + (i / 2)] = static_cast<std::byte>((nibbles[i] & 0x0FU) | ((nibbles[i + 1] & 0x0FU) << 4U));
    }
    return block;
}

inline std::array<std::uint8_t, AdpcmSamplesPerBlock> nibblesOf(std::uint8_t value) {
    std::array<std::uint8_t, AdpcmSamplesPerBlock> nibbles{};
    nibbles.fill(value);
    return nibbles;
}

// A sample of `blocks` blocks of constant nibbles, flags given per block.
inline std::vector<std::byte> adpcmSample(std::span<const std::uint8_t> flags, std::uint8_t nibble = 1) {
    std::vector<std::byte> bytes;
    for (const auto flag : flags) {
        const auto block = adpcmBlock(0, 12, flag, nibblesOf(nibble));
        bytes.insert(bytes.end(), block.begin(), block.end());
    }
    return bytes;
}

// A tone of a synthetic VAB.
struct ToneSpec {
    std::uint8_t min = 60;
    std::uint8_t max = 60;
    std::uint8_t center = 60;
    std::uint8_t shift = 0;
    std::uint8_t vol = 127;
    std::uint8_t pan = 64;
    std::uint8_t mode = 0;
    std::uint8_t pbmax = 2;
    std::uint16_t adsr1 = 0x80FF;
    std::uint16_t adsr2 = 0x5FC0;
    std::int16_t vag = 1;
};

// A program of a synthetic VAB, in its slot.
struct ProgramSpec {
    std::size_t slot = 0;
    std::vector<ToneSpec> tones;
    std::uint8_t mvol = 127;
    std::uint8_t mpan = 64;
};

// Builds a VH (version 7) and returns it; vagSizes are byte sizes of samples 1.., divided by 8 in the table.
inline std::vector<std::byte> buildVab(const std::vector<ProgramSpec>& programs,
                                       const std::vector<std::size_t>& vagSizes, std::uint16_t vs) {
    const std::size_t vhSize = vabHeaderSize(programs.size());
    std::size_t vbSize = 0;
    for (const auto size : vagSizes) {
        vbSize += size;
    }
    std::size_t toneCount = 0;
    for (const auto& program : programs) {
        toneCount += program.tones.size();
    }
    Bytes b;
    b.text("pBAV").u32(7).u32(0).u32(static_cast<std::uint32_t>(vhSize + vbSize));
    b.u16(0).u16(static_cast<std::uint32_t>(programs.size())).u16(static_cast<std::uint32_t>(toneCount)).u16(vs);
    b.u8(127).u8(64).u8(0).u8(0).u32(0);
    for (std::size_t slot = 0; slot < VabProgramSlots; ++slot) {
        const ProgramSpec* program = nullptr;
        for (const auto& candidate : programs) {
            if (candidate.slot == slot) {
                program = &candidate;
            }
        }
        if (program == nullptr) {
            b.u8(0).u8(127).u8(0).u8(0xFF).u8(64).u8(0).u16(0).u32(0).u32(0);
        } else {
            b.u8(static_cast<std::uint32_t>(program->tones.size())).u8(program->mvol).u8(0).u8(0xFF);
            b.u8(program->mpan).u8(0).u16(0).u32(0).u32(0);
        }
    }
    for (const auto& program : programs) {
        for (std::size_t i = 0; i < VabTonesPerProgram; ++i) {
            if (i >= program.tones.size()) {
                b.fill(32);
                continue;
            }
            const auto& tone = program.tones[i];
            b.u8(0).u8(tone.mode).u8(tone.vol).u8(tone.pan).u8(tone.center).u8(tone.shift).u8(tone.min).u8(tone.max);
            b.u8(0).u8(0).u8(0).u8(0).u8(tone.pbmax).u8(tone.pbmax).u8(0).u8(0);
            b.u16(tone.adsr1).u16(tone.adsr2).u16(static_cast<std::uint32_t>(program.slot));
            b.u16(static_cast<std::uint16_t>(tone.vag)).fill(8);
        }
    }
    b.u16(0);
    for (std::size_t i = 1; i < VabVagSlots; ++i) {
        b.u16(i <= vagSizes.size() ? static_cast<std::uint32_t>(vagSizes[i - 1] / 8) : 0);
    }
    return b.bytes();
}

// A sound bank VHB: {12, vb, vbEnd}, the VH and the VB.
inline std::vector<std::byte> buildBankVhb(std::span<const std::byte> vh, std::span<const std::byte> vb) {
    Bytes b;
    const auto vbOffset = static_cast<std::uint32_t>(12 + vh.size());
    b.u32(12).u32(vbOffset).u32(static_cast<std::uint32_t>(vbOffset + vb.size())).append(vh).append(vb);
    return b.bytes();
}

// A music font VHB: {16, vb, seq, seqEnd}, the VH, the VB and the SEQ bank.
inline std::vector<std::byte> buildFontVhb(std::span<const std::byte> vh, std::span<const std::byte> vb,
                                           std::span<const std::byte> seqBank) {
    Bytes b;
    const auto vbOffset = static_cast<std::uint32_t>(16 + vh.size());
    const auto seqOffset = static_cast<std::uint32_t>(vbOffset + vb.size());
    b.u32(16).u32(vbOffset).u32(seqOffset).u32(static_cast<std::uint32_t>(seqOffset + seqBank.size()));
    b.append(vh).append(vb).append(seqBank);
    return b.bytes();
}

} // namespace dwe::formats::audio::test
