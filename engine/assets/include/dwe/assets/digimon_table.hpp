#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/core/error.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {

class VirtualFileSystem;

// The table of every Digimon, `data/digimon` (content/schemas/digimon.schema.json): the executable's DIGIMON_DATA
// (params.h: DigimonPara). References to other tables are their ids, or the raw number as text when the extractor
// found it outside its table.
struct DigimonRow {
    std::string id;
    // The original's Digimon id, which the other tables and the code use.
    std::int32_t index = 0;
    std::string name;
    std::string fileName;
    std::int32_t boneCount = 0;
    std::int32_t radius = 0;
    std::int32_t height = 0;
    std::int32_t type = 0;
    // 1 baby, 2 in-training, 3 rookie, 4 champion, 5 ultimate; 0 for the entries that are no partner.
    std::int32_t level = 0;
    std::array<std::optional<std::int32_t>, 3> special{};
    std::optional<std::string> dropItem;
    std::int32_t dropChance = 0;
    std::array<std::optional<std::string>, 16> moves{};
};

struct DigimonTable {
    std::vector<DigimonRow> digimon;

    // The row with this id, or nullptr.
    [[nodiscard]] const DigimonRow* find(std::string_view id) const;
};

// Parses the text of `data/digimon`. Unknown and missing keys are errors.
core::Result<DigimonTable> parseDigimonTable(std::string_view yaml);

// Reads and parses the digimon table at `id` (a ruleset names it by role) from the virtual file system.
core::Result<DigimonTable> loadDigimonTable(const VirtualFileSystem& vfs, const AssetId& id);

} // namespace dwe::assets
