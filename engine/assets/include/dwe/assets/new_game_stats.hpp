#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {

class VirtualFileSystem;

// The stats a partner starts a new game with.
struct StartingStats {
    std::int32_t offense = 0;
    std::int32_t defense = 0;
    std::int32_t speed = 0;
    std::int32_t brains = 0;
    std::int32_t maxHp = 0;
    std::int32_t maxMp = 0;
    std::int32_t hp = 0;
    std::int32_t mp = 0;

    friend bool operator==(const StartingStats&, const StartingStats&) = default;
};

struct PartnerStartingStats {
    // A Digimon id, or a raw type number as text the extractor could not name.
    std::string digimon;
    StartingStats stats;
};

// `data/new_game_stats` (content/schemas/new_game_stats.schema.json): the stats the new-game script sets
// (formats::script::findNewGameStats), per partner it asks about and for any other.
struct NewGameStatsTable {
    // The DG.SCN script and the offset of its IF, for the citations.
    std::int32_t script = 0;
    std::int32_t at = 0;
    std::vector<PartnerStartingStats> partners;
    StartingStats others;

    // The stats of a partner: its own, or the others'.
    [[nodiscard]] const StartingStats& statsOf(std::string_view digimon) const;
};

// Parses the text of `data/new_game_stats`. Unknown and missing keys are errors.
core::Result<NewGameStatsTable> parseNewGameStats(std::string_view yaml);

// Reads and parses `data/new_game_stats` from the virtual file system; nothing when no pack has it.
core::Result<std::optional<NewGameStatsTable>> loadNewGameStats(const VirtualFileSystem& vfs);

} // namespace dwe::assets
