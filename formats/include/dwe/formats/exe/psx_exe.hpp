#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace dwe::formats::exe {

// Size of the executable's header; the text segment follows it in the file.
constexpr std::size_t PsxExeHeaderSize = 0x800;

// The header fields of a PS-X EXE that place it in memory (names as in the PsyQ EXEC structure).
struct PsxExeHeader {
    std::uint32_t pc0;   // entry point
    std::uint32_t gp0;   // initial $gp
    std::uint32_t tAddr; // load address of the text segment
    std::uint32_t tSize; // size of the text segment, which is the rest of the file
};

// Reads and checks the header: the "PS-X EXE" magic and a text segment that fits in the file.
core::Result<PsxExeHeader> readPsxExeHeader(std::span<const std::byte> executable);

// Returns the file offset of count bytes at a memory address, if they lie inside the text segment.
std::optional<std::size_t> fileOffsetOf(const PsxExeHeader& header, std::uint32_t address, std::size_t count);

} // namespace dwe::formats::exe
