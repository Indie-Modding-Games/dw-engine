#pragma once

#include <dwe/assets/table_composition.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {

class VirtualFileSystem;

// The table of every field map, `data/maps` (content/schemas/maps.schema.json, docs/formats/mapas.md): the
// executable's MAP_ENTRIES and the tables it points into. Numbers are kept as the extractor wrote them.

// A map of the table (MapEntry).
struct MapEntry {
    // The data's folder, `map/<id>/`: the file name in lowercase, or map_<index> for entries without a file.
    std::string id;
    // The original's map id, which warps use. The engine numbers the maps a pack adds after the original's; their
    // number depends on the packs mounted.
    std::int32_t index = 0;
    // Whether the base pack gave the index (the original's), not the engine.
    bool originalIndex = true;
    std::string filename;
    std::int32_t images8bpp = 0;
    std::int32_t images4bpp = 0;
    // 0x1F sound id, 0x40 fixed time of day, 0x80 has NPCs; 0x20 unknown.
    std::int32_t flags = 0;
    std::int32_t soundId = 0;
    // The background always uses its first palette (map.c: updateTimeOfDay).
    bool fixedTimeOfDay = false;
    bool hasNpcs = false;
    // Index into MapTable::doors.
    std::optional<std::int32_t> doors;
    // Index into MapTable::toilets.
    std::optional<std::int32_t> toilet;
    // Index into MapTable::mapNames: the name shown when the map loads.
    std::int32_t loadingName = 0;

    // Whether the entry has a map file.
    [[nodiscard]] bool hasFile() const { return !filename.empty(); }
};

// A door model placed on a map (DoorStruct).
struct MapDoor {
    // DOOR\DOORnn.TMD, or nothing for an unused slot.
    std::optional<std::int32_t> modelId;
    glm::ivec3 position{};
    std::int32_t rotation = 0;
};

struct MapDoorSet {
    std::int32_t index = 0;
    std::array<MapDoor, 6> doors{};
};

// The toilet's rectangle on a map, in the original's x and z.
struct MapToilet {
    std::int32_t index = 0;
    std::int32_t x1 = 0;
    std::int32_t z1 = 0;
    std::int32_t x2 = 0;
    std::int32_t z2 = 0;
};

// Object instances of a map shown only by day or by night (utils2.c: updateMapLightState).
struct MapLightUpdate {
    // The map id, or the raw index as text when the extractor could not name it.
    std::string map;
    // 0 = shown by day, 1 = shown by night.
    std::int32_t mode = 0;
    std::int32_t startId = 0;
    std::int32_t count = 0;
    // The trigger that enables it, or nothing for always.
    std::optional<std::int32_t> trigger;
};

struct MapTable {
    std::vector<MapEntry> maps;
    std::vector<MapDoorSet> doors;
    std::vector<MapToilet> toilets;
    // As stored, padded with spaces; displayName trims them.
    std::vector<std::string> mapNames;
    std::vector<MapLightUpdate> lightUpdates;

    // The map with this id, or nullptr.
    [[nodiscard]] const MapEntry* find(std::string_view id) const;

    // The map with this index, the original's or the engine's, or nullptr.
    [[nodiscard]] const MapEntry* findByIndex(std::int32_t index) const;

    // The map of the original with this index (MapEntry::originalIndex), or nullptr: for a warp's raw target_map.
    [[nodiscard]] const MapEntry* findByOriginalIndex(std::int32_t index) const;

    // The name shown when a map loads, without the padding; empty if its loadingName is outside mapNames.
    [[nodiscard]] std::string_view displayName(const MapEntry& entry) const;

    // The doors of a map, or nullptr.
    [[nodiscard]] const MapDoorSet* doorsOf(const MapEntry& entry) const;
};

// How the packs' files of the table compose: maps by id; doors, toilets, map names and light updates are numbered by
// position or by map index, so a pack that has them replaces each of those sections whole (table_composition.hpp).
inline constexpr std::array<EntrySection, 1> MapEntrySections{{{.section = "maps", .entryKey = "id"}}};
inline constexpr TableComposition MapComposition{.sections = MapEntrySections};

// Parses the text of `data/maps`. Unknown and missing keys are errors. `engineNumbered` are the ids of the maps whose
// index the engine gave (ComposedTableText).
core::Result<MapTable> parseMapTable(std::string_view yaml,
                                     const std::set<std::string, std::less<>>& engineNumbered = {});

// Reads and parses `data/maps` from the virtual file system.
core::Result<MapTable> loadMapTable(const VirtualFileSystem& vfs);

} // namespace dwe::assets
