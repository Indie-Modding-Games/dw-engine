#include "overlay_extractor.hpp"

#include <dwe/core/yaml.hpp>
#include <dwe/formats/exe/memory_image.hpp>
#include <dwe/formats/exe/overlay.hpp>
#include <dwe/formats/exe/overlay_tables.hpp>
#include <dwe/formats/exe/overlay_yaml.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "asset_names.hpp"

namespace dwe::dwx {

namespace {

using formats::exe::FileRange;
using formats::exe::MemoryImage;
using formats::exe::OverlayDataKind;
using formats::exe::OverlayDataRange;
using formats::exe::OverlayLayout;
using formats::exe::OverlayParts;
using formats::exe::OverlayTablesLayout;
using formats::exe::TablesYaml;

constexpr std::string_view OverlaySuffix = "_REL.BIN";
constexpr std::string_view TrainingPath = "TRN_REL.BIN";
// The overlay of the movie player: what it stores past its data are the buffers of the decoder (mov_bss_us.c).
constexpr std::string_view MoviePath = "MOV_REL.BIN";

constexpr std::array TablePaths = {std::string_view{"FISH_REL.BIN"}, std::string_view{"STD_REL.BIN"},
                                   std::string_view{"VS_REL.BIN"}, TrainingPath, std::string_view{"TRN2_REL.BIN"}};

bool hasGameplayTables(std::string_view path) {
    return std::ranges::contains(TablePaths, path);
}

std::string range(const FileRange& part) {
    return std::format("[0x{:X}, 0x{:X})", part.begin, part.end);
}

bool allZero(std::span<const std::byte> bytes) {
    return std::ranges::all_of(bytes, [](std::byte value) { return value == std::byte{0}; });
}

core::Result<std::vector<std::byte>> readDiscFile(const Disc& disc, std::string_view path) {
    const formats::disc::IsoFile* file = disc.findFile(path);
    if (file == nullptr) {
        return core::fail(std::format("{} is not on the disc", path));
    }
    return formats::disc::readIsoFile(disc.source(), *file);
}

// Adds the documents of converted tables to the extraction.
core::Result<void> addDocuments(const TablesYaml& yaml, Extraction& extraction) {
    for (const auto& document : yaml.documents) {
        auto id = names::tableDocument(document.name);
        if (!id) {
            return std::unexpected{id.error()};
        }
        const auto text = core::emitYaml(document.document);
        if (!text) {
            return std::unexpected{text.error()};
        }
        const auto bytes = std::as_bytes(std::span{*text});
        extraction.outputs.push_back(
            {.id = *id, .extension = "yaml", .bytes = std::vector<std::byte>(bytes.begin(), bytes.end())});
        extraction.notes.push_back(std::format("its gameplay tables are converted to data/{}", document.name));
    }
    extraction.notes.insert(extraction.notes.end(), yaml.notes.begin(), yaml.notes.end());
    return {};
}

core::Result<MemoryImage> mapExecutable(const Disc& disc, std::vector<std::byte>& storage) {
    auto bytes = readDiscFile(disc, disc.info().executablePath);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    storage = std::move(*bytes);
    return MemoryImage::fromExecutable(storage);
}

// Checks the files the overlay carries against the loose files of the disc.
void checkEmbeddedFiles(const Disc& disc, std::span<const std::byte> file, const OverlayLayout& layout,
                        Extraction& extraction) {
    for (const auto& embedded : layout.embeddedFiles) {
        const FileRange part{.begin = embedded.offset, .end = embedded.offset + embedded.size};
        const auto loose = readDiscFile(disc, embedded.discPath);
        if (!loose) {
            extraction.unknowns.push_back(std::format("{}: should be {}, which could not be read: {}", range(part),
                                                      embedded.discPath, loose.error().message));
            continue;
        }
        if (part.end > file.size() || !std::ranges::equal(file.subspan(part.begin, part.size()), *loose)) {
            extraction.unknowns.push_back(
                std::format("{}: the bytes are not those of {}, which every known overlay carries there", range(part),
                            embedded.discPath));
            continue;
        }
        extraction.notes.push_back(
            std::format("{}: same bytes as {}; the asset is taken from that file", range(part), embedded.discPath));
    }
}

std::string_view kindName(OverlayDataKind kind) {
    switch (kind) {
    case OverlayDataKind::JumpTable:
        return "jump table of";
    case OverlayDataKind::Pointers:
        return "pointers:";
    case OverlayDataKind::Numbers:
        return "numbers:";
    case OverlayDataKind::Text:
        return "text:";
    }
    return "";
}

core::Result<std::span<const std::byte>> executableCopy(const core::Result<MemoryImage>& executable,
                                                        std::uint32_t address, std::size_t size) {
    if (!executable) {
        return std::unexpected{executable.error()};
    }
    return executable->bytesAt(address, size);
}

// Notes the data range by range, comparing the ranges the executable also has with its copy.
void noteDataRanges(const Disc& disc, std::span<const std::byte> file, const OverlayLayout& layout,
                    const OverlayParts& parts, Extraction& extraction) {
    extraction.notes.push_back(std::format("{}: data of the code, mapped from the code of the {} build, whose symbols "
                                           "name it; none of it is an asset",
                                           range(parts.data), layout.build));
    std::vector<std::byte> storage;
    const auto executable = mapExecutable(disc, storage);
    for (const OverlayDataRange& data : parts.dataRanges) {
        const FileRange part{.begin = data.begin, .end = data.end};
        std::string note = std::format("{}: {} {}", range(part), kindName(data.kind), data.what);
        if (data.executableAddress != 0) {
            const auto copy = executableCopy(executable, data.executableAddress, part.size());
            if (!copy) {
                extraction.unknowns.push_back(std::format("{}: could not be compared with the executable's copy: {}",
                                                          range(part), copy.error().message));
                continue;
            }
            const auto bytes = file.subspan(part.begin, part.size());
            // Counted over the two byte spans: the MSVC STL counts an iota of std::size_t in a 128-bit integer that
            // std::format cannot print.
            const auto differ = std::ranges::count_if(
                std::views::zip(bytes, *copy), [](const auto& pair) { return std::get<0>(pair) != std::get<1>(pair); });
            note += differ == 0
                        ? std::format("; same bytes as the executable's copy at 0x{:08X}", data.executableAddress)
                        : std::format("; {} of its bytes differ from the executable's copy at 0x{:08X}", differ,
                                      data.executableAddress);
        }
        extraction.notes.push_back(std::move(note));
    }
}

void noteParts(const Disc& disc, std::span<const std::byte> file, const OverlayLayout& layout,
               const OverlayParts& parts, Extraction& extraction) {
    const FileRange header{.begin = layout.headerOffset, .end = layout.headerOffset + formats::exe::OverlayHeaderSize};
    extraction.notes.push_back(std::format("{}: header, id {}, name \"{}\"", range(header), parts.header.id,
                                           formats::exe::escapeGameText(parts.header.name)));
    if (!layout.note.empty()) {
        extraction.notes.push_back(std::format("[0x0, 0x{:X}): {}", file.size(), layout.note));
    }
    extraction.notes.push_back(std::format("{}: code; it is not an asset", range(parts.code)));
    if (!layout.dataRanges.empty()) {
        noteDataRanges(disc, file, layout, parts, extraction);
    } else if (parts.data.size() != 0) {
        extraction.notes.push_back(std::format(
            "{}: data of the code (jump tables, texts, sprite, camera and effect constants), all of it named by the "
            "decomp's symbols; {}",
            range(parts.data),
            hasGameplayTables(layout.path) ? "only its gameplay tables are assets" : "none of it is an asset"));
    }
    if (parts.storedMemory.size() != 0) {
        const std::string_view what = layout.path == MoviePath ? "the movie decoder's buffers" : "memory";
        const std::string_view held = allZero(file.subspan(parts.storedMemory.begin, parts.storedMemory.size()))
                                          ? "all zero"
                                          : "leftover bytes of the build";
        extraction.notes.push_back(std::format("{}: {} the overlay only uses at run time, stored as the file was "
                                               "built ({}); it is not an asset",
                                               range(parts.storedMemory), what, held));
    }
    extraction.notes.push_back(std::format("{}: the zero word every overlay ends with", range(parts.trailer)));
}

core::Result<formats::exe::TrainingTables> readTraining(const Disc& disc, const OverlayTablesLayout& tables) {
    const auto layout = formats::exe::findOverlayLayout(disc.info().version, TrainingPath);
    if (!layout) {
        return core::fail(std::format("no layout for {}", TrainingPath));
    }
    const auto bytes = readDiscFile(disc, TrainingPath);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    return formats::exe::readTrainingTables(MemoryImage::fromOverlay(*bytes, layout->loadAddress), tables.training);
}

// Converts the gameplay tables of the overlays that have them.
core::Result<void> convertTables(const Disc& disc, std::span<const std::byte> file, const OverlayLayout& layout,
                                 Extraction& extraction) {
    const std::string_view path = layout.path;
    if (!hasGameplayTables(path)) {
        return {};
    }
    const auto tables = formats::exe::findOverlayTablesLayout(disc.info().version);
    const GameTables* game = disc.tables();
    if (!tables || game == nullptr) {
        return core::fail("the tables of this version are not known");
    }
    const MemoryImage overlay = MemoryImage::fromOverlay(file, layout.loadAddress);
    if (path == "FISH_REL.BIN") {
        const auto read = formats::exe::readFishingTables(overlay, tables->fishing);
        if (!read) {
            return std::unexpected{read.error()};
        }
        return addDocuments(formats::exe::fishingToYaml(*read, game->ids), extraction);
    }
    if (path == "STD_REL.BIN" || path == "VS_REL.BIN") {
        std::vector<std::byte> storage;
        const auto executable = mapExecutable(disc, storage);
        if (!executable) {
            return std::unexpected{executable.error()};
        }
        const bool stadium = path == "STD_REL.BIN";
        const auto read =
            formats::exe::readBattleStageTables(overlay, *executable, stadium ? tables->stadium : tables->versus);
        if (!read) {
            return std::unexpected{read.error()};
        }
        extraction.notes.emplace_back("the arenas' object and TIM counts are read from the executable, where the "
                                      "original keeps them");
        return addDocuments(formats::exe::battleStageToYaml(*read, stadium ? "stadium" : "versus"), extraction);
    }
    if (path == TrainingPath) {
        const auto read = formats::exe::readTrainingTables(overlay, tables->training);
        if (!read) {
            return std::unexpected{read.error()};
        }
        return addDocuments(formats::exe::trainingToYaml(*read, game->ids), extraction);
    }
    // TRN2_REL.BIN: a second build of the training code, without the moves it teaches.
    const auto own = formats::exe::readTrainingTables(overlay, tables->training2);
    if (!own) {
        return std::unexpected{own.error()};
    }
    const auto first = readTraining(disc, *tables);
    if (!first) {
        return std::unexpected{first.error()};
    }
    if (own->animations != first->animations || own->slotReels != first->slotReels ||
        own->assistRolls != first->assistRolls || own->spots != first->spots) {
        return core::fail(std::format("its training tables differ from those of {}", TrainingPath));
    }
    extraction.notes.push_back(std::format("its training tables are the same as those of {} (data/training); it has "
                                           "none of the moves training teaches",
                                           TrainingPath));
    return {};
}

} // namespace

bool handlesOverlay(std::string_view path) {
    return path.ends_with(OverlaySuffix) && path.size() > OverlaySuffix.size() && !path.contains('/');
}

core::Result<Extraction> extractOverlay(const Disc& disc, const formats::disc::IsoFile& file) {
    const auto bytes = formats::disc::readIsoFile(disc.source(), file);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    Extraction extraction;
    const auto layout = formats::exe::findOverlayLayout(disc.info().version, file.path);
    if (!layout) {
        extraction.unknowns.push_back(
            std::format("[0x0, 0x{:X}): no layout of this overlay for this version", bytes->size()));
        return extraction;
    }
    const auto parts = formats::exe::readOverlayParts(*bytes, *layout);
    if (!parts) {
        extraction.unknowns.push_back(std::format("[0x0, 0x{:X}): {}", bytes->size(), parts.error().message));
        return extraction;
    }
    extraction.unknowns.insert(extraction.unknowns.end(), parts->mismatches.begin(), parts->mismatches.end());
    checkEmbeddedFiles(disc, *bytes, *layout, extraction);
    noteParts(disc, *bytes, *layout, *parts, extraction);
    if (const auto converted = convertTables(disc, *bytes, *layout, extraction); !converted) {
        extraction.unknowns.push_back(std::format("{}: the gameplay tables in the data were not converted: {}",
                                                  range(parts->data), converted.error().message));
    }
    return extraction;
}

} // namespace dwe::dwx
