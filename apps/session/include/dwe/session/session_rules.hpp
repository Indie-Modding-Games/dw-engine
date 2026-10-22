#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace dwe::session {

// The session's own ruleset sections: what no single game system owns.
inline constexpr std::string_view NewGameSection = "new_game";
inline constexpr std::string_view TamerSection = "tamer";

// How a new game starts (rules/<id>/new_game.yaml).
struct NewGameRules {
  // The Digimon id the partner starts as.
  std::string partner;
  // Its maximum stats; current HP and MP start full.
  game::partner::StatBlock stats;
  std::int32_t tamerLevel = 0;
};

// The tamer's rules (rules/<id>/tamer.yaml).
struct TamerRules {
  std::int32_t levelMin = 0;
  std::int32_t levelMax = 0;
  // Levels gained when an evolution's tamer chance succeeds (evl.c: EVL_applyEvolution).
  std::int32_t evolutionLevelGain = 0;
};

// Reads the sections; errors name the file, line and column.
core::Result<NewGameRules> readNewGameRules(const ruleset::Section& section);
core::Result<TamerRules> readTamerRules(const ruleset::Section& section);

// Reads both from a ruleset, which must have them, and checks the start level against the limits.
struct SessionRules {
  NewGameRules newGame;
  TamerRules tamer;
};
core::Result<SessionRules> readSessionRules(const ruleset::Ruleset& ruleset);

// The checks of the session's sections for the ruleset store.
ruleset::SectionChecks sessionSectionChecks();

} // namespace dwe::session
