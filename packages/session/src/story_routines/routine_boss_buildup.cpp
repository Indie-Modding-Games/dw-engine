#include <dwe/core/log.hpp>
#include <dwe/session/story_props.hpp>

#include <cstdint>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 40;
// script_interp.c:1858: EAB_startBuildup(ENTITY_TABLE[scriptIdToEntityId(5)]): the map's first
// NPC, the final boss set_digimon put there.
constexpr std::int64_t BossCharacter = 5;

} // namespace

// Routine 40: the final boss's charge on Mt. Infinity's top (script_interp.c:1856, eab.c:649
// EAB_startBuildup); the section goes on while it runs, until the partner's routine 8 brings the
// boss in. The app hides the boss, draws the rings and plays the hum.
core::Result<void> registerBossBuildupRoutine(StoryScenesContext& scenes) {
  return scenes.routines.registerRoutine(
      Routine,
      [&scenes](game::story::RoutineNumber /*routine*/) -> core::Result<game::story::RoutineStart> {
        core::logDebug("[port:story] call_routine 40: EAB_startBuildup on character {}",
                       BossCharacter);
        scenes.propRequests.emplace_back(BossBuildupStarted{.character = BossCharacter});
        return game::story::RoutineGoesOn{};
      });
}

} // namespace dwe::session
