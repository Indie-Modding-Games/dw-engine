#include <dwe/core/log.hpp>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 22;

} // namespace

// Routine 22: the tournaments' medal. The original goes on at once when no cup set earns one
// (tournament.c:168 checkTournamentMedalConditions gives -1) and else waits while it gives the
// medal and its text box (tournament.c:242 scriptCheckTournamentMedal). The cups and the medals are
// the tournaments' (the arena's lane): the app's screen decides and closes.
core::Result<void> registerTournamentMedalRoutine(StoryScenesContext& scenes) {
  return scenes.routines.registerRoutine(
      Routine,
      [&scenes](game::story::RoutineNumber routine) -> core::Result<game::story::RoutineStart> {
        core::logDebug("[port:story] call_routine 22 (script_interp.c:1662): the medal check is "
                       "the tournaments'");
        return scenes.openScreen({.screen = StoryScreen::TournamentMedal,
                                  .routine = routine,
                                  .first = 0,
                                  .second = 0,
                                  .waits = true});
      });
}

} // namespace dwe::session
