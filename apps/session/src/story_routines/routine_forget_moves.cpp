#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 47;

} // namespace

// Routine 47: after the partner lost all its lives it forgets some of its moves, and a box says
// so; the section waits until the box is closed (script_interp.c:494, script_common.c:2456
// lostAllLives). The moves and the box are the app's (the rebirth lane's).
core::Result<void> registerForgetMovesRoutine(StoryScenesContext& scenes) {
  return registerScreenRoutine(scenes, Routine, StoryScreen::ForgetMoves, true);
}

} // namespace dwe::session
