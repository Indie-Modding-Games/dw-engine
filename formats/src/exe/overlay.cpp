#include <dwe/formats/exe/overlay.hpp>

#include <bytes/byte_cursor.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <optional>
#include <string>

namespace dwe::formats::exe {

namespace {

using bytes::ByteCursor;

constexpr std::array<std::byte, 4> Magic = {std::byte{'M'}, std::byte{'W'}, std::byte{'o'}, std::byte{'1'}};
constexpr std::size_t IdPadding = 3;     // bytes 5 to 7
constexpr std::size_t SizesPadding = 12; // bytes 0x14 to 0x1F
constexpr std::size_t NameSize = 0x20;

bool allZero(std::span<const std::byte> bytes) {
    return std::ranges::all_of(bytes, [](std::byte value) { return value == std::byte{0}; });
}

// BTL, STD and VS start with the same five files of ETCDAT and ETCHI (btl.yaml, std.yaml, vs.yaml).
constexpr std::array BattleEmbeddedFiles = {
    OverlayEmbeddedFile{.offset = 0x0, .size = 0xD20, .discPath = "ETCDAT/EX.TIM"},
    OverlayEmbeddedFile{.offset = 0xD20, .size = 0x1038, .discPath = "ETCDAT/EX.TMD"},
    OverlayEmbeddedFile{.offset = 0x1D58, .size = 0x4C8, .discPath = "ETCDAT/FEEL_EF.TMD"},
    OverlayEmbeddedFile{.offset = 0x2220, .size = 0x628, .discPath = "ETCHI/MAHI.TMD"},
    OverlayEmbeddedFile{.offset = 0x2848, .size = 0x1940, .discPath = "ETCDAT/DEFEFF.TMD"},
};
constexpr std::size_t BattleHeaderOffset = 0x4188;

// An overlay with its header at the start of the file.
constexpr OverlayLayout plain(std::string_view path, std::uint32_t loadAddress,
                              std::optional<std::size_t> storedMemoryOffset) {
    return {.version = "us",
            .path = path,
            .loadAddress = loadAddress,
            .headerOffset = 0,
            .embeddedFiles = {},
            .storedMemoryOffset = storedMemoryOffset,
            .build = "us",
            .note = {},
            .dataRanges = {}};
}

// An overlay the decomp does not build for its version: only its header says where its code and data are.
constexpr OverlayLayout unbuilt(std::string_view path, std::uint32_t loadAddress) {
    OverlayLayout layout = plain(path, loadAddress, std::nullopt);
    layout.build = {};
    return layout;
}

// A battle overlay: the embedded files, then the header.
constexpr OverlayLayout battle(std::string_view path, std::size_t storedMemoryOffset) {
    return {.version = "us",
            .path = path,
            .loadAddress = 0x80052AE0,
            .headerOffset = BattleHeaderOffset,
            .embeddedFiles = BattleEmbeddedFiles,
            .storedMemoryOffset = storedMemoryOffset,
            .build = "us",
            .note = {},
            .dataRanges = {}};
}

// US: load addresses from utils2.c and overlay.ld; header offsets and the start of the *_bss_us data from
// config/us/<overlay>.yaml. Neither SHOP_REL.BIN nor DGET_REL.BIN has a stored-memory offset: SHOP has no build in
// the decomp (its header still gives its code and data) and DGET has no *_bss_us data.
constexpr std::array Layouts = {
    battle("BTL_REL.BIN", 0x21374),
    battle("STD_REL.BIN", 0x28B84),
    battle("VS_REL.BIN", 0x1EBB0),
    plain("FISH_REL.BIN", 0x80070000, 0xAB40),
    plain("EVL_REL.BIN", 0x80060000, 0x3F3C),
    plain("KAR_REL.BIN", 0x80053800, 0x7DA0),
    plain("MOV_REL.BIN", 0x80010000, 0x9EC),
    plain("DOO2_REL.BIN", 0x80070000, 0x1AE4),
    plain("DOOA_REL.BIN", 0x80080000, 0x49C0),
    plain("TRN_REL.BIN", 0x80088800, 0x6B20),
    unbuilt("SHOP_REL.BIN", 0x80080800),
    plain("DGET_REL.BIN", 0x80080800, std::nullopt),
    plain("TRN2_REL.BIN", 0x80088800, 0x541C),
    plain("MURD_REL.BIN", 0x8007C000, 0x1820),
    plain("ENDI_REL.BIN", 0x80060000, 0xD98),
    plain("EAB_REL.BIN", 0x80060000, 0x17D0),
};

// Checks one range of the data map; returns why it does not hold what the map says, or nothing.
std::optional<std::string> checkDataRange(std::span<const std::byte> file, const OverlayLayout& layout,
                                          const OverlayParts& parts, const OverlayDataRange& range) {
    const auto bytes = file.subspan(range.begin, range.end - range.begin);
    if (range.kind == OverlayDataKind::Numbers) {
        return std::nullopt;
    }
    if (range.kind == OverlayDataKind::Text) {
        if (bytes.back() != std::byte{0}) {
            return "the text does not end with a zero";
        }
        return std::nullopt;
    }
    if (range.begin % 4 != 0 || bytes.size() % 4 != 0) {
        return "the addresses are not whole aligned words";
    }
    const FileRange target = range.kind == OverlayDataKind::JumpTable ? parts.code : parts.data;
    const std::uint64_t first = std::uint64_t{layout.loadAddress} + target.begin;
    const std::uint64_t last = std::uint64_t{layout.loadAddress} + target.end;
    ByteCursor c{bytes};
    while (c.remaining() != 0) {
        const std::size_t at = range.begin + c.offset();
        const std::uint32_t address = c.u32();
        const bool zero = address == 0 && range.kind == OverlayDataKind::Pointers;
        const bool aligned = range.kind == OverlayDataKind::Pointers || address % 4 == 0;
        if (!zero && (address < first || address >= last || !aligned)) {
            return std::format("the word at 0x{:X} (0x{:08X}) does not point into the {}", at, address,
                               range.kind == OverlayDataKind::JumpTable ? "code" : "data");
        }
    }
    return std::nullopt;
}

// Checks that the data map covers the data end to end and that every range holds what the map says; keeps the ranges
// that do.
void checkDataRanges(std::span<const std::byte> file, const OverlayLayout& layout, OverlayParts& parts) {
    std::size_t next = parts.data.begin;
    for (const OverlayDataRange& range : layout.dataRanges) {
        if (range.begin != next || range.end <= range.begin || range.end > parts.data.end) {
            parts.mismatches.push_back(std::format("[0x{:X}, 0x{:X}): the data map does not cover the data end to end; "
                                                   "its range [0x{:X}, 0x{:X}) does not come next",
                                                   next, parts.data.end, range.begin, range.end));
            return;
        }
        if (const auto why = checkDataRange(file, layout, parts, range)) {
            parts.mismatches.push_back(
                std::format("[0x{:X}, 0x{:X}): {}: {}", range.begin, range.end, range.what, *why));
        } else {
            parts.dataRanges.push_back(range);
        }
        next = range.end;
    }
    if (next != parts.data.end) {
        parts.mismatches.push_back(
            std::format("[0x{:X}, 0x{:X}): the data map stops before the end of the data", next, parts.data.end));
    }
}

} // namespace

core::Result<OverlayHeader> readOverlayHeader(std::span<const std::byte> file, std::size_t offset) {
    if (offset > file.size() || file.size() - offset < OverlayHeaderSize) {
        return core::fail(std::format("no room for a 0x{:X}-byte header at 0x{:X}", OverlayHeaderSize, offset));
    }
    ByteCursor c{file.subspan(offset, OverlayHeaderSize)};
    if (!std::ranges::equal(c.take(Magic.size()), Magic)) {
        return core::fail(std::format("no \"MWo1\" header at 0x{:X}", offset));
    }
    OverlayHeader header;
    header.id = c.u8();
    const bool idPadded = allZero(c.take(IdPadding));
    header.address = c.u32();
    header.codeSize = c.u32();
    header.dataSize = c.u32();
    const bool sizesPadded = allZero(c.take(SizesPadding));
    const auto name = c.take(NameSize);
    const auto end = std::ranges::find(name, std::byte{0});
    if (!idPadded || !sizesPadded || end == name.end() || !allZero({end, name.end()})) {
        return core::fail(std::format("the header at 0x{:X} has bytes that are not zero where every known one has "
                                      "zeros, or a name without a NUL",
                                      offset));
    }
    std::ranges::transform(name.begin(), end, std::back_inserter(header.name),
                           [](std::byte value) { return static_cast<char>(value); });
    return header;
}

std::span<const OverlayLayout> overlayLayouts() {
    return Layouts;
}

std::optional<OverlayLayout> findOverlayLayout(std::string_view version, std::string_view path) {
    for (const OverlayLayout& layout : Layouts) {
        if (layout.version == version && layout.path == path) {
            return layout;
        }
    }
    return std::nullopt;
}

core::Result<OverlayParts> readOverlayParts(std::span<const std::byte> file, const OverlayLayout& layout) {
    auto header = readOverlayHeader(file, layout.headerOffset);
    if (!header) {
        return std::unexpected{header.error()};
    }
    OverlayParts parts;
    parts.header = std::move(*header);
    const std::uint64_t expectedAddress = std::uint64_t{layout.loadAddress} + layout.headerOffset;
    if (parts.header.address != expectedAddress) {
        parts.mismatches.push_back(std::format("the header says it sits at 0x{:08X}; the layout puts it at 0x{:08X}",
                                               parts.header.address, expectedAddress));
    }
    parts.embedded = {.begin = 0, .end = layout.headerOffset};
    const std::size_t codeBegin = layout.headerOffset + OverlayHeaderSize;
    const std::uint64_t dataEnd = std::uint64_t{codeBegin} + parts.header.codeSize + parts.header.dataSize;
    if (file.size() < OverlayTrailerSize || dataEnd != file.size() - OverlayTrailerSize) {
        parts.mismatches.push_back(
            std::format("the header's code and data end at 0x{:X}, not 4 bytes before the end of "
                        "the file (0x{:X})",
                        dataEnd, file.size()));
        parts.code = {.begin = codeBegin, .end = std::min<std::size_t>(file.size(), codeBegin + parts.header.codeSize)};
        parts.data = {.begin = parts.code.end, .end = file.size()};
        parts.storedMemory = parts.trailer = {.begin = file.size(), .end = file.size()};
        return parts;
    }
    parts.code = {.begin = codeBegin, .end = codeBegin + parts.header.codeSize};
    parts.trailer = {.begin = file.size() - OverlayTrailerSize, .end = file.size()};
    std::size_t storedBegin = parts.trailer.begin;
    if (layout.storedMemoryOffset) {
        if (*layout.storedMemoryOffset < parts.code.end || *layout.storedMemoryOffset > parts.trailer.begin) {
            parts.mismatches.push_back(
                std::format("the layout's stored memory at 0x{:X} is outside the data", *layout.storedMemoryOffset));
        } else {
            storedBegin = *layout.storedMemoryOffset;
        }
    }
    parts.data = {.begin = parts.code.end, .end = storedBegin};
    parts.storedMemory = {.begin = storedBegin, .end = parts.trailer.begin};
    if (!allZero(file.subspan(parts.trailer.begin))) {
        parts.mismatches.emplace_back("the last word of the file is not zero");
    }
    if (!layout.dataRanges.empty()) {
        checkDataRanges(file, layout, parts);
    }
    return parts;
}

} // namespace dwe::formats::exe
