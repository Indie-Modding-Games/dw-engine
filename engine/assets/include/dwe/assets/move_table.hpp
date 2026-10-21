#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/core/error.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {

class VirtualFileSystem;

// The move table, `data/moves` (content/schemas/moves.schema.json): the executable's MOVE_DATA. Numbers are kept as
// the extractor wrote them.

// A move (params.h: MoveData).
struct MoveEntry {
    std::string id;
    // The move's index in MOVE_DATA, which the Digimon table's move slots and the learned moves use.
    std::int32_t index = 0;
    // Its name, escaped as the extractor writes it; nothing for none.
    std::optional<std::string> name = std::nullopt;
    std::int32_t distance = 0;
    std::int32_t power = 0;
    std::int32_t mpCost = 0;
    std::int32_t iframes = 0;
    std::int32_t range = 0;
    std::int32_t special = 0;
    std::int32_t status = 0;
    std::int32_t accuracy = 0;
    std::int32_t statusChance = 0;
    std::int32_t unknown0d = 0;
    std::int32_t unknown0e = 0;
    std::int32_t unknown0f = 0;
};

struct MoveTable {
    std::vector<MoveEntry> moves;

    // The move with this id, or nullptr.
    [[nodiscard]] const MoveEntry* find(std::string_view id) const;

    // The move with this index, or nullptr.
    [[nodiscard]] const MoveEntry* findByIndex(std::int32_t index) const;
};

// Parses the text of `data/moves`. Unknown and missing keys are errors.
core::Result<MoveTable> parseMoveTable(std::string_view yaml);

// Reads and parses a move table of the packs, `data/moves` or the one a ruleset names.
core::Result<MoveTable> loadMoveTable(const VirtualFileSystem& vfs, const AssetId& id);

} // namespace dwe::assets
