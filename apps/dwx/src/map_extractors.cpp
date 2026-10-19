#include "map_extractors.hpp"

#include <dwe/core/yaml.hpp>
#include <dwe/formats/image/png.hpp>
#include <dwe/formats/image/tim.hpp>
#include <dwe/formats/image/tim_packs.hpp>
#include <dwe/formats/map/map_file.hpp>
#include <dwe/formats/map/map_yaml.hpp>
#include <dwe/formats/map/tfs_file.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "asset_names.hpp"
#include "game_tables.hpp"
#include "image_extractors.hpp"

namespace dwe::dwx {

namespace {

using formats::exe::MapEntry;
using formats::exe::MapEntryFlags;

constexpr std::string_view MapFolder = "MAP/MAP";
constexpr std::string_view MapExtension = ".MAP";
constexpr std::string_view BackgroundExtension = ".TFS";
constexpr std::string_view ArenaFolder = "STDDAT/";
constexpr std::string_view ArenaCollisionExtension = ".ATR";
// The palettes the original switches between: day, dusk and night (map.c: updateTimeOfDay).
constexpr std::size_t KnownPalettes = 3;
// setupMap uploads palette i of the .TFS to video memory row 481 + i (map.c: setupMap).
constexpr int FirstPaletteRow = 481;
// A sprite whose clut is 16 or more reads the 8-bit CLUT at row clut + 468 (overworld.c: buildMapOverlayPrim).
constexpr int FirstSpriteRowClut = 16;
constexpr int SpriteRowBase = 468;
// A bit of MapEntry::flags that no known code reads.
constexpr std::uint8_t UnreadFlag = 0x20;

// A .MAP file, read with the MAP_ENTRIES record that names it, or with the options its own bytes give when no
// record names it.
struct LoadedMap {
    std::optional<std::size_t> id;       // the first record that names the file
    std::vector<std::size_t> sharedWith; // other records that name it
    formats::map::MapReadOptions options;
    std::string slug;
    std::vector<std::byte> bytes;
    formats::map::MapFile map;
};

// Palettes past the third are only seen through the map's sprites: one whose clut points at the palette's row reads
// it. A palette that no sprite reads is never shown; one that a sprite reads is reported, since a TIM of the .MAP may
// put its own CLUT in the same row.
void notePalettesPastTheThird(const formats::map::MapFile& map, std::size_t paletteCount, Extraction& extraction) {
    std::vector<std::size_t> unread;
    for (std::size_t palette = KnownPalettes; palette < paletteCount; ++palette) {
        const int row = FirstPaletteRow + static_cast<int>(palette);
        const auto reads =
            !map.objects ? 0
                         : std::ranges::count_if(map.objects->sprites, [row](const formats::map::MapSprite& sprite) {
                               return sprite.clut >= FirstSpriteRowClut && sprite.clut + SpriteRowBase == row;
                           });
        if (reads == 0) {
            unread.push_back(palette);
        } else {
            extraction.unknowns.push_back(std::format(
                "palette {}: {} sprite(s) read its video memory row {}, which a TIM of the map may also fill", palette,
                reads, row));
        }
    }
    if (unread.empty()) {
        return;
    }
    std::string which = unread.size() == 1 ? "palette" : "palettes";
    for (std::size_t i = 0; i < unread.size(); ++i) {
        std::string_view separator = ",";
        if (i == 0) {
            separator = "";
        } else if (i + 1 == unread.size()) {
            separator = " and";
        }
        which += std::format("{} {}", separator, unread[i]);
    }
    which += unread.size() == 1 ? " is" : " are";
    extraction.notes.push_back(std::format("{} never shown: the original uploads every palette to video memory, "
                                           "switches between the first three and no sprite of the map reads the rest",
                                           which));
}

bool isMapPath(std::string_view path, std::string_view extension) {
    return path.starts_with(MapFolder) && path.ends_with(extension);
}

// The slug of a map that no record names: its file name, unless a record's id already has it.
core::Result<std::string> orphanSlug(const GameTables& tables, std::string_view mapPath) {
    const std::string_view name = mapPath.substr(mapPath.rfind('/') + 1);
    std::string slug = names::segment(name.substr(0, name.rfind('.')));
    if (std::ranges::contains(tables.ids.maps, slug)) {
        return core::fail(
            std::format("no MAP_ENTRIES record names {}, and its name {} is a record's id", mapPath, slug));
    }
    return slug;
}

// Reads the .MAP file at mapPath with its MAP_ENTRIES record: the record says how many TIMs the file has and whether
// it has NPC records. A file no record names gets them from its own bytes, if they settle them.
core::Result<LoadedMap> loadMap(const Disc& disc, std::string_view mapPath) {
    const GameTables* tables = disc.tables();
    if (tables == nullptr) {
        return core::fail("the executable's tables are not known; a map needs its MAP_ENTRIES record");
    }
    LoadedMap loaded;
    std::vector<std::size_t> ids;
    const auto& entries = tables->tables.mapEntries;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].filename.empty() && mapVolumePath(i, entries[i].filename, MapExtension) == mapPath) {
            ids.push_back(i);
        }
    }
    const formats::disc::IsoFile* file = disc.findFile(mapPath);
    if (file == nullptr) {
        return core::fail(std::format("{} is not on the disc", mapPath));
    }
    auto bytes = formats::disc::readIsoFile(disc.source(), *file);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    if (ids.empty()) {
        auto options = formats::map::inferMapReadOptions(*bytes);
        if (!options) {
            return core::fail(
                std::format("no MAP_ENTRIES record names {}, and the file does not settle its options: {}", mapPath,
                            options.error().message));
        }
        auto slug = orphanSlug(*tables, mapPath);
        if (!slug) {
            return std::unexpected{slug.error()};
        }
        loaded.options = *options;
        loaded.slug = std::move(*slug);
    } else {
        const MapEntry& entry = entries[ids.front()];
        loaded.options = {.imageCount8 = entry.num8bppImages,
                          .imageCount4 = entry.num4bppImages,
                          .hasNpcs = (entry.flags & MapEntryFlags::HasNpcs) != 0};
        loaded.id = ids.front();
        loaded.sharedWith.assign(ids.begin() + 1, ids.end());
        loaded.slug = tables->ids.maps[ids.front()];
    }
    auto map = formats::map::readMapFile(*bytes, loaded.options);
    if (!map) {
        return std::unexpected{map.error()};
    }
    loaded.bytes = std::move(*bytes);
    loaded.map = std::move(*map);
    return loaded;
}

// Notes the other records that name the same file, which is extracted once under the first record's slug, or that no
// record names it.
void noteEntries(const Disc& disc, const LoadedMap& loaded, Extraction& extraction) {
    if (!loaded.id) {
        extraction.notes.push_back(
            std::format("no MAP_ENTRIES record names the file, so the game never loads it; it is extracted as {} "
                        "with the options its bytes give: {} 8-bit and {} 4-bit TIMs by their modes, no NPC records",
                        loaded.slug, loaded.options.imageCount8, loaded.options.imageCount4));
    }
    for (const std::size_t id : loaded.sharedWith) {
        extraction.notes.push_back(std::format("MAP_ENTRIES[{}] ({}) names the same file; it is extracted as {}", id,
                                               disc.tables()->ids.maps[id], loaded.slug));
    }
}

core::Result<OutputFile> yamlOutput(const core::Result<assets::AssetId>& id, const core::YamlNode& node) {
    if (!id) {
        return std::unexpected{id.error()};
    }
    const auto text = core::emitYaml(node);
    if (!text) {
        return std::unexpected{text.error()};
    }
    const auto bytes = std::as_bytes(std::span{*text});
    return OutputFile{.id = *id, .extension = "yaml", .bytes = std::vector<std::byte>(bytes.begin(), bytes.end())};
}

// Adds the TIMs of one image section. A section holds one TIM; anything after it that is not zero is reported.
core::Result<void> addSectionImages(Extraction& extraction, const LoadedMap& loaded, std::string_view source,
                                    const formats::map::MapImageSection& section, std::size_t& index) {
    const auto bytes = std::span{loaded.bytes}.subspan(section.offset, section.size);
    const auto list = formats::image::readTimSequence(bytes);
    if (!list || list->entries.empty()) {
        extraction.unknowns.push_back(std::format("{}-bit image section at 0x{:x}: {}", section.bitsPerPixel,
                                                  section.offset,
                                                  list ? "it does not start with a TIM" : list.error().message));
        return {};
    }
    for (const auto& [offset, tim] : list->entries) {
        const auto id = names::mapImage(loaded.slug, section.bitsPerPixel, index);
        if (!id) {
            return std::unexpected{id.error()};
        }
        const TimPlace place{.source = source, .entry = index, .offset = section.offset + offset};
        if (auto added = addTimImage(extraction, *id, tim, place); !added) {
            return added;
        }
        ++index;
    }
    const auto rest = bytes.subspan(list->end);
    if (std::ranges::any_of(rest, [](std::byte value) { return value != std::byte{0}; })) {
        extraction.unknowns.push_back(std::format("{}-bit image section at 0x{:x}: {} bytes after its images",
                                                  section.bitsPerPixel, section.offset, rest.size()));
    }
    return {};
}

std::string replaceExtension(std::string_view path, std::string_view extension) {
    return std::string{path.substr(0, path.rfind('.'))} + std::string{extension};
}

} // namespace

bool handlesMapFile(std::string_view path) {
    return isMapPath(path, MapExtension);
}

core::Result<Extraction> extractMapFile(const Disc& disc, const formats::disc::IsoFile& file) {
    const auto loaded = loadMap(disc, file.path);
    if (!loaded) {
        return std::unexpected{loaded.error()};
    }
    const GameTables& tables = *disc.tables();
    Extraction extraction;
    auto data =
        yamlOutput(names::mapAsset(loaded->slug, "data"),
                   formats::map::mapToYaml(loaded->map, {.maps = tables.ids.maps, .digimon = tables.ids.digimon}));
    if (!data) {
        return std::unexpected{data.error()};
    }
    extraction.outputs.push_back(std::move(*data));

    std::size_t images8 = 0;
    std::size_t images4 = 0;
    for (const auto& section : loaded->map.images) {
        std::size_t& index = section.bitsPerPixel == 8 ? images8 : images4;
        if (auto added = addSectionImages(extraction, *loaded, file.path, section, index); !added) {
            return std::unexpected{added.error()};
        }
    }
    if (loaded->id && (tables.tables.mapEntries[*loaded->id].flags & UnreadFlag) != 0) {
        extraction.unknowns.push_back(
            std::format("MAP_ENTRIES[{}] sets flag 0x{:02x}, which no known code reads", *loaded->id, UnreadFlag));
    }
    noteEntries(disc, *loaded, extraction);
    return extraction;
}

bool handlesMapBackground(std::string_view path) {
    return isMapPath(path, BackgroundExtension);
}

core::Result<Extraction> extractMapBackground(const Disc& disc, const formats::disc::IsoFile& file) {
    const auto loaded = loadMap(disc, replaceExtension(file.path, MapExtension));
    if (!loaded) {
        return std::unexpected{loaded.error()};
    }
    const auto bytes = formats::disc::readIsoFile(disc.source(), file);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    const auto tfs = formats::map::readTfsFile(*bytes);
    if (!tfs) {
        return std::unexpected{tfs.error()};
    }
    const auto backgrounds = formats::map::composeBackgrounds(*tfs, loaded->map.setup);
    if (!backgrounds) {
        return std::unexpected{backgrounds.error()};
    }

    Extraction extraction;
    for (std::size_t i = 0; i < backgrounds->size(); ++i) {
        const auto& image = (*backgrounds)[i];
        std::vector<formats::image::Rgba8> palette;
        std::ranges::transform(image.palette, std::back_inserter(palette), formats::image::toRgba8);
        auto png = formats::image::encodePngIndexed(static_cast<std::uint32_t>(image.width),
                                                    static_cast<std::uint32_t>(image.height), image.indices, palette);
        if (!png) {
            return std::unexpected{png.error()};
        }
        const auto id = names::mapAsset(loaded->slug, "background_" + formats::map::backgroundVariantName(i));
        if (!id) {
            return std::unexpected{id.error()};
        }
        extraction.outputs.push_back(OutputFile{.id = *id, .extension = "png", .bytes = std::move(*png)});
    }

    const auto used = static_cast<std::size_t>(
        std::ranges::count_if(loaded->map.setup.tiles, [](std::int32_t tile) { return tile != -1; }));
    if (tfs->tiles.size() > used) {
        extraction.unknowns.push_back(
            std::format("{} of its {} tiles are not on the map's grid", tfs->tiles.size() - used, tfs->tiles.size()));
    }
    notePalettesPastTheThird(loaded->map, tfs->palettes.size(), extraction);
    if (loaded->id) {
        const std::uint8_t flags = disc.tables()->tables.mapEntries[*loaded->id].flags;
        if ((flags & MapEntryFlags::NoTimeOfDay) != 0 && tfs->palettes.size() > 1) {
            extraction.notes.emplace_back(
                "the map has a fixed time of day: the original only shows the day background");
        } else if ((flags & MapEntryFlags::NoTimeOfDay) == 0 && tfs->palettes.size() < KnownPalettes) {
            extraction.notes.push_back(
                std::format("the map changes with the time of day but has {} palette(s)", tfs->palettes.size()));
        }
    }
    noteEntries(disc, *loaded, extraction);
    return extraction;
}

bool handlesArenaCollision(std::string_view path) {
    return path.starts_with(ArenaFolder) && path.ends_with(ArenaCollisionExtension);
}

core::Result<Extraction> extractArenaCollision(const Disc& disc, const formats::disc::IsoFile& file) {
    const auto bytes = formats::disc::readIsoFile(disc.source(), file);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    constexpr std::size_t GridSize = formats::map::CollisionSize * formats::map::CollisionSize;
    if (bytes->size() != GridSize) {
        return core::fail(std::format("a collision grid has {} bytes, this file {}", GridSize, bytes->size()));
    }
    std::vector<std::uint8_t> cells(GridSize);
    std::ranges::transform(*bytes, cells.begin(), [](std::byte value) { return std::to_integer<std::uint8_t>(value); });
    auto output = yamlOutput(names::arenaCollision(file.path), formats::map::collisionToYaml(cells));
    if (!output) {
        return std::unexpected{output.error()};
    }
    Extraction extraction;
    extraction.outputs.push_back(std::move(*output));
    return extraction;
}

} // namespace dwe::dwx
