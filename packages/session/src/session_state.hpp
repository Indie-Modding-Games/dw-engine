#pragma once

#include <dwe/game/field/field.hpp>
#include <dwe/game/partner/partner.hpp>
#include <dwe/game/partner/rules.hpp>
#include <dwe/game/partner/species.hpp>
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/ruleset/random.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/session_rules.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace dwe::session {

// The systems' random numbers start from the seed after the partner's, so the two do not repeat
// each other.
inline constexpr std::uint64_t SystemsSeedOffset = 1;

// What a session keeps of the playthrough, which its save sections write and read. Not movable: the
// partner points to the rules here.
struct SessionState {
  SessionState(const ruleset::ClockRules& clockRules, game::partner::PartnerRules rules,
               const game::partner::SpeciesTable& species, std::uint64_t seed)
      : clock(clockRules), partnerRules(std::move(rules)), partner(partnerRules, species, seed),
        random(seed + SystemsSeedOffset) {}

  SessionState(const SessionState&) = delete;
  SessionState& operator=(const SessionState&) = delete;
  SessionState(SessionState&&) = delete;
  SessionState& operator=(SessionState&&) = delete;
  ~SessionState() = default;

  // Where the player is now: on the attached field's map, else where the loaded save left them.
  [[nodiscard]] std::optional<FieldPlace> currentPlace() const;
  // Where a save made now continues: the current place, on the map the ending's save names when
  // there is one (main_menu.c: gameClearSave puts getFileCityTopMap in CURRENT_SCREEN and keeps the
  // player's position).
  [[nodiscard]] std::optional<FieldPlace> continuePlace() const;

  ruleset::GameClock clock;
  game::partner::PartnerRules partnerRules;
  game::partner::Partner partner;
  // The random numbers of the systems besides the partner.
  ruleset::Random random;
  SessionRules rules;
  bool playing = false;
  TamerState tamer;
  std::optional<FieldPlace> savedPlace;
  // The game was finished (MAIN_STATE 2): a save made now loads into the after-game with its
  // movie, and continues on `clearedMap` when the ending's save named one.
  bool cleared = false;
  std::optional<std::string> clearedMap;
  const game::field::Field* field = nullptr;
};

} // namespace dwe::session
