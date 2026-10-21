#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/table_composition.hpp>
#include <dwe/assets/table_handle.hpp>
#include <dwe/core/error.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
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
    // The move's number: in the original, its index in MOVE_DATA. The engine numbers the moves a pack adds after the
    // original's; their number depends on the packs mounted.
    std::int32_t index = 0;
    // Whether `index` is the original's (the base pack gave it): only those are covered by a rule over the original's
    // numbers, such as a range of a ruleset.
    bool originalIndex = true;
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

class MoveTable;

// A move of one composition of the tables, as the running game holds it (table_handle.hpp).
using MoveHandle = TableHandle<MoveTable>;

class MoveTable {
public:
    // An empty table.
    MoveTable();

    // A table of these moves, a composition of its own.
    explicit MoveTable(std::vector<MoveEntry> moves);

    [[nodiscard]] std::span<const MoveEntry> entries() const { return moves_; }

    // The move with this id, or nullptr.
    [[nodiscard]] const MoveEntry* find(std::string_view id) const;

    // The handle of the move with this id, if the table has it.
    [[nodiscard]] std::optional<MoveHandle> handleOf(std::string_view id) const;

    // The handle of an entry of this table.
    [[nodiscard]] MoveHandle handleOf(const MoveEntry& entry) const;

    // Whether the handle is of this composition of the table.
    [[nodiscard]] bool owns(MoveHandle handle) const;

    // The move of a handle of this table; a handle of another composition stops the program (it is a bug: the
    // session's tables never change).
    [[nodiscard]] const MoveEntry& at(MoveHandle handle) const;

    // The move with this original number (MoveEntry::originalIndex), or nullptr: for rules over the original's
    // numbers. Everything else goes by id or handle.
    [[nodiscard]] const MoveEntry* findByOriginalIndex(std::int32_t index) const;

private:
    std::vector<MoveEntry> moves_;
    CompositionGeneration generation_;
};

// How the packs' files of the table compose: moves by id (table_composition.hpp).
inline constexpr std::array<EntrySection, 1> MoveEntrySections{{{.section = "moves", .entryKey = "id"}}};
inline constexpr TableComposition MoveComposition{.sections = MoveEntrySections};

// Parses the text of `data/moves`. Unknown and missing keys, and an id or index listed twice, are errors.
// The moves of `engineNumbered` have the number the engine gave them (ComposedTableText); the others, the original's.
core::Result<MoveTable> parseMoveTable(std::string_view yaml,
                                       const std::set<std::string, std::less<>>& engineNumbered = {});

// Reads and parses a move table of the packs, `data/moves` or the one a ruleset names.
core::Result<MoveTable> loadMoveTable(const VirtualFileSystem& vfs, const AssetId& id);

} // namespace dwe::assets
