#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/table_composition.hpp>
#include <dwe/core/error.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {

class VirtualFileSystem;

// The move table, `data/moves` (content/schemas/moves.schema.json): the executable's MOVE_DATA and MOVE_NAMES, and
// MOVE_LEARN_CHANCES where the extractor wrote it. Numbers are kept as the extractor wrote them.

// The chance (%) of learning a move in battle, by the special slot of the learner that matches the move's special
// (battle_ui.c: battleMoveLearning).
using LearnChances = std::array<std::int32_t, 3>;

// A move (params.h: MoveData).
struct MoveEntry {
    std::string id;
    // The move's index in MOVE_DATA, which the Digimon table's move slots and the learned moves use.
    std::int32_t index = 0;
    // Its name, escaped as the extractor writes it; nothing for none.
    std::optional<std::string> name = std::nullopt;
    // The squared distance a ranged move is used from.
    std::int32_t distance = 0;
    std::int32_t power = 0;
    // A third of the MP the move costs.
    std::int32_t mpCost = 0;
    // The frames the move keeps a fighter it hits from being hit again.
    std::int32_t iframes = 0;
    // 1 melee, 2 ranged, 3 area, 4 on oneself.
    std::int32_t range = 0;
    std::int32_t special = 0;
    // 0 none, 1 poison, 2 confusion, 3 stun, 4 flatten.
    std::int32_t status = 0;
    std::int32_t accuracy = 0;
    std::int32_t statusChance = 0;
    // `unknown_0d` (move.h: Move unk3). Bit 0: it needs a target; bit 1: it shares a cooldown with the other moves
    // that set it.
    std::int32_t flags = 0;
    std::int32_t unknown0e = 0;
    std::int32_t unknown0f = 0;
    // Only the moves that can be learned have them, and only in tables extracted since they were added.
    std::optional<LearnChances> learnChances = std::nullopt;
};

struct MoveTable {
    std::vector<MoveEntry> moves;

    // The move with this id, or nullptr.
    [[nodiscard]] const MoveEntry* find(std::string_view id) const;

    // The move with this index, or nullptr.
    [[nodiscard]] const MoveEntry* findByIndex(std::int32_t index) const;
};

// How the packs' files of the table compose: moves by id (table_composition.hpp).
inline constexpr std::array<EntrySection, 1> MoveEntrySections{{{.section = "moves", .entryKey = "id"}}};
inline constexpr TableComposition MoveComposition{.sections = MoveEntrySections};

// Parses the text of `data/moves`. Unknown and missing keys, and an id or index listed twice, are errors.
core::Result<MoveTable> parseMoveTable(std::string_view yaml);

// Reads and parses a move table of the packs, `data/moves` or the one a ruleset names.
core::Result<MoveTable> loadMoveTable(const VirtualFileSystem& vfs, const AssetId& id);

} // namespace dwe::assets
