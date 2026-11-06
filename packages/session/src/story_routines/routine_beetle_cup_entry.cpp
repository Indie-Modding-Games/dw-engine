#include <dwe/core/log.hpp>
#include <dwe/session/game_session.hpp>

#include <array>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 33;

// script_interp.c:1796: the entry's triggers and variables.
constexpr std::array<std::int64_t, 3> EntryTriggers{0x25, 0x26, 0x27};
constexpr std::int64_t EntryDayVariable = 2;
constexpr std::int64_t CupVariable = 3;
constexpr std::int64_t EntrantVariable = 4;
constexpr std::int32_t BeetleCup = 0x16;
// Entered on another day: the cup's day with bit 7, which the calendar's next day clears
// (script_common.c: dailyPStatTrigger keeps variable 2's low seven bits).
constexpr std::int64_t EnteredBeforeTheDay = 0x95;

} // namespace

BeetleCupEntry beetleCupEntry(std::int32_t day, std::int64_t partnerSpecies) {
  return {.entryDay = day != BeetleCup ? EnteredBeforeTheDay : std::int64_t{BeetleCup},
          .cup = BeetleCup,
          .entrant = partnerSpecies};
}

// Routine 33: the Beetle Cup's entry (betl02, script 102). It sets triggers 37 to 39, variable 2
// to 0x95 (0x16 when today is day 0x16), variable 3 to the cup (0x16) and variable 4 to the
// partner's species, and goes on in the same frame.
core::Result<void> registerBeetleCupEntryRoutine(StoryScenesContext& scenes) {
  return scenes.routines.registerRoutine(
      Routine,
      [&scenes](game::story::RoutineNumber /*routine*/) -> core::Result<game::story::RoutineStart> {
        if (scenes.session == nullptr || !scenes.digimon) {
          core::logWarning("[port:story] call_routine 33 (script_interp.c:1796): no game to enter");
          return game::story::RoutineGoesOn{};
        }
        const std::string& species = scenes.session->partner().state().species;
        const assets::DigimonRow* row = scenes.digimon->find(species);
        const BeetleCupEntry entry =
            beetleCupEntry(scenes.session->clock().time().day, row != nullptr ? row->index : 0);
        game::story::StoryStatePort& state = scenes.state();
        for (const std::int64_t trigger : EntryTriggers) {
          if (auto set = state.setTrigger(trigger, true); !set) {
            return std::unexpected{set.error()};
          }
        }
        for (const auto [variable, value] : std::array<std::pair<std::int64_t, std::int64_t>, 3>{
                 {{EntryDayVariable, entry.entryDay},
                  {CupVariable, entry.cup},
                  {EntrantVariable, entry.entrant}}}) {
          if (auto set = state.setVariable(variable, value); !set) {
            return std::unexpected{set.error()};
          }
        }
        core::logDebug("[port:story] call_routine 33 (script_interp.c:1796): Beetle Cup entry "
                       "day {:#x}, entrant {}",
                       entry.entryDay, entry.entrant);
        return game::story::RoutineGoesOn{};
      });
}

} // namespace dwe::session
