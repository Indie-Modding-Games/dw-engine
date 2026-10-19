#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/exe/memory_image.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Where the strings of the executable and the overlays are, per version. The strings are not indexed anywhere in
// the files, so this list is curated from the decomp: the pointer and record tables of the code, and the named
// strings of the text blocks (text/<dir>/<name>.yaml, with the addresses of config/<version>/symbols*.txt). Only
// addresses and counts are kept here, never the text.
namespace dwe::formats::text {

enum class StringStorage : std::uint8_t {
    Pointers, // char*[count]; a NULL entry has no string
    Records,  // count records of `stride` bytes that start with char name[length]
    Embedded, // char[count][stride]
    Named,    // one string at each address of `addresses`
};

struct StringBlock {
    std::string_view source; // the overlay's file ("BTL_REL.BIN"), or empty for the main executable
    std::string_view name;   // unique in its source; from the decomp's table symbol or text block
    StringStorage storage = StringStorage::Named;
    std::uint32_t address = 0;
    std::uint32_t count = 0;
    std::uint32_t stride = 0;
    std::uint32_t length = 0;
    std::span<const std::uint32_t> addresses;
};

// An overlay and the address it is loaded at (utils2.c: loadDynamicLibrary, REL_BIN_OFFSETS).
struct OverlayLoad {
    std::string_view file;
    std::uint32_t loadAddress = 0;
};

struct StringManifest {
    std::string_view version;
    std::string_view language; // the folder of the text tables: "en"
    std::span<const OverlayLoad> overlays;
    std::span<const StringBlock> blocks;
    // Strings the decomp names but whose address is not known (no symbol): "<text yaml> <block>: <count>".
    std::span<const std::string_view> missing;
};

// Returns the manifest of a version, if the tools know it (only "us" so far).
std::optional<StringManifest> findStringManifest(std::string_view version);

// Reads the raw bytes of every string of a block, without their NUL; a NULL pointer gives nothing. Strings are
// looked up in the block's own image and then in the main executable's, where overlays also point. A fixed-size
// field without a NUL is the whole field.
core::Result<std::vector<std::optional<std::string>>>
readStringBlock(const StringBlock& block, const exe::MemoryImage& image, const exe::MemoryImage& executable);

} // namespace dwe::formats::text
