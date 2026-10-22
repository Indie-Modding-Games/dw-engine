#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/formats/script/disassembly.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// The partner's stats at the start of a new game. No C code sets them: the event script that runs
// after the introduction does, with SET_STAT (script_interp.c: SCRIPT_OP_SET_STAT), one run of them
// per partner it asks about.
namespace dwe::formats::script {

// The DG.SCN script that sets them.
inline constexpr std::size_t NewGameScript = 193;

// The stats SET_STAT writes, in its order (include/dw/script.h: SCRIPT_STAT_*): offense, defense,
// speed, brains, max HP, max MP, current HP, current MP.
inline constexpr std::size_t StartingStatCount = 8;
using StartingStats = std::array<std::int32_t, StartingStatCount>;

struct NewGameStats {
  // The offset of the IF that picks the partner.
  std::uint32_t at = 0;
  // The Digimon type (STORE_DIGIMON_TYPE) the IF asks for, and the stats it gets.
  std::int32_t partnerType = 0;
  StartingStats partner{};
  // The stats of any other partner.
  StartingStats others{};
};

// Finds the stats in a disassembly: a STORE_DIGIMON_TYPE into a variable, an IF that tests that
// variable for one type and jumps away otherwise, a run of every SET_STAT and a JUMP_TO over the
// other branch, which is a run of every SET_STAT at the IF's target. Fails if the script has no
// such block.
core::Result<NewGameStats> findNewGameStats(const Disassembly& disassembly);

// The `data/new_game_stats` document (content/schemas/new_game_stats.schema.json); `partnerId` is
// the Digimon id of the type the IF asks for.
core::YamlNode newGameStatsToYaml(const NewGameStats& stats, const std::string& partnerId);

} // namespace dwe::formats::script
