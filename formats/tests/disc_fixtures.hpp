#pragma once

// Synthetic disc fixtures built in memory: raw sectors, a small ISO9660 volume and a fake executable. Nothing here
// comes from a real disc.

#include <dwe/formats/disc/sector.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace fixtures {

using dwe::formats::disc::Form1DataSize;
using dwe::formats::disc::Form2DataSize;
using dwe::formats::disc::RawSectorSize;
using dwe::formats::disc::SectorForm;

inline void putU8(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
}

inline void putU16(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) {
    putU8(bytes, offset, value);
    putU8(bytes, offset + 1, value >> 8U);
}

inline void putU32(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) {
    putU16(bytes, offset, value);
    putU16(bytes, offset + 2, value >> 16U);
}

// ISO9660 "both-endian" field: little-endian then big-endian copy.
inline void putU32Both(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) {
    putU32(bytes, offset, value);
    for (std::size_t i = 0; i < 4; ++i) {
        putU8(bytes, offset + 4 + i, value >> (8U * (3 - i)));
    }
}

inline void putText(std::span<std::byte> bytes, std::size_t offset, std::string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        bytes[offset + i] = static_cast<std::byte>(text[i]);
    }
}

// Wraps user data in a raw sector: sync, header with the given mode, subheader (mode 2) and the data.
inline std::vector<std::byte> makeRawSector(std::span<const std::byte> userData, int mode, SectorForm form) {
    std::vector<std::byte> sector(RawSectorSize);
    putU8(sector, 0, 0x00);
    for (std::size_t i = 1; i < 11; ++i) {
        putU8(sector, i, 0xFF);
    }
    putU8(sector, 11, 0x00);
    putU8(sector, 15, static_cast<std::uint32_t>(mode));
    std::size_t dataOffset = 16;
    if (mode == 2) {
        const std::uint32_t submode = form == SectorForm::Form2 ? 0x20U : 0x08U;
        putU8(sector, 18, submode);
        putU8(sector, 22, submode);
        dataOffset = 24;
    }
    const std::size_t capacity = form == SectorForm::Form2 ? Form2DataSize : Form1DataSize;
    for (std::size_t i = 0; i < userData.size() && i < capacity; ++i) {
        sector[dataOffset + i] = userData[i];
    }
    return sector;
}

// Turns a cooked 2048-byte image into a raw mode 2 form 1 image.
inline std::vector<std::byte> toRawImage(std::span<const std::byte> cooked) {
    std::vector<std::byte> raw;
    for (std::size_t offset = 0; offset + Form1DataSize <= cooked.size(); offset += Form1DataSize) {
        const auto sector = makeRawSector(cooked.subspan(offset, Form1DataSize), 2, SectorForm::Form1);
        raw.insert(raw.end(), sector.begin(), sector.end());
    }
    return raw;
}

} // namespace fixtures
