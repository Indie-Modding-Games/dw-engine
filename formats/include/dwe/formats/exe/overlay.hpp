#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The overlays (*_REL.BIN): code and data that the main executable reads whole to a fixed address when a scene needs
// them (utils2.c: loadDynamicLibrary). The loader does not look at the file; the layout of each one comes from the
// decomp's build (config/<version>/<overlay>.yaml and overlay.ld).
namespace dwe::formats::exe {

inline constexpr std::size_t OverlayHeaderSize = 0x40;
// Every overlay ends with a zero word after its data (overlay.ld).
inline constexpr std::size_t OverlayTrailerSize = 4;

// The 0x40-byte header: "MWo1", an id byte, its own memory address, the sizes of the code and of the data that
// follow it and a name. The rest of it is zero.
struct OverlayHeader {
    std::uint8_t id = 0;
    std::uint32_t address = 0;
    std::uint32_t codeSize = 0;
    std::uint32_t dataSize = 0;
    std::string name;

    bool operator==(const OverlayHeader&) const = default;
};

// Reads the header at an offset of the file. Fails if the magic is wrong, the name has no NUL or a byte that is zero
// in every known overlay is not.
core::Result<OverlayHeader> readOverlayHeader(std::span<const std::byte> file, std::size_t offset);

// A file of the disc that an overlay carries before its header, byte for byte.
struct OverlayEmbeddedFile {
    std::size_t offset = 0;
    std::size_t size = 0;
    std::string_view discPath;
};

// What one range of an overlay's data holds.
enum class OverlayDataKind : std::uint8_t {
    JumpTable, // the case addresses of one switch: every word points into the code
    Pointers,  // addresses into the data, or zero
    Numbers,   // a table of numbers the code reads
    Text,      // strings, each padded with zeros
};

// A range of an overlay's data, mapped from the code that reads it: a lui/addiu or lui/lw pair, or a jr through a
// jump table.
struct OverlayDataRange {
    std::size_t begin = 0; // file offsets
    std::size_t end = 0;
    OverlayDataKind kind = OverlayDataKind::Numbers;
    std::string_view what; // what the bytes are and which functions read them
    // Where the main executable has a copy of the bytes, 0 if it has none.
    std::uint32_t executableAddress = 0;
};

// Where things are in one overlay of one version.
struct OverlayLayout {
    std::string_view version;
    std::string_view path; // on the disc
    std::uint32_t loadAddress = 0;
    std::size_t headerOffset = 0; // after the embedded files, if there are any
    std::span<const OverlayEmbeddedFile> embeddedFiles;
    // Where the memory the overlay only uses at run time starts (the *_bss_us sources): the linker stored whatever was
    // in that memory when the file was built. Absent when the data runs to the trailer.
    std::optional<std::size_t> storedMemoryOffset;
    // The decomp configuration whose build has the same bytes as the file, so that its symbols name every table and
    // constant of the data: the version itself, another one when the disc carries a leftover build, or empty if no
    // build matches.
    std::string_view build;
    // What the version does with the file, when it is not what the build says; empty otherwise.
    std::string_view note;
    // The data range by range, for an overlay whose build is not the version's own; empty otherwise.
    std::span<const OverlayDataRange> dataRanges;
};

// Returns every overlay layout the tools know.
std::span<const OverlayLayout> overlayLayouts();

// Returns the layout of an overlay of a version, if the tools know it.
std::optional<OverlayLayout> findOverlayLayout(std::string_view version, std::string_view path);

// A range of file offsets, [begin, end).
struct FileRange {
    std::size_t begin = 0;
    std::size_t end = 0;

    [[nodiscard]] std::size_t size() const { return end - begin; }
    bool operator==(const FileRange&) const = default;
};

// An overlay file cut along its layout and header.
struct OverlayParts {
    OverlayHeader header;
    FileRange embedded; // the embedded files, empty if there are none
    FileRange code;
    FileRange data;         // the initialised data: the code's constants and tables
    FileRange storedMemory; // empty if the layout has none
    FileRange trailer;
    // The ranges of the layout's data map that hold what the map says.
    std::vector<OverlayDataRange> dataRanges;
    // Where the file does not match its layout: header address, sizes that do not add up, a trailer that is not zero,
    // data ranges that do not cover the data or hold addresses outside the code or the data.
    std::vector<std::string> mismatches;
};

// Cuts an overlay file. Fails only if there is no header where the layout puts it.
core::Result<OverlayParts> readOverlayParts(std::span<const std::byte> file, const OverlayLayout& layout);

} // namespace dwe::formats::exe
