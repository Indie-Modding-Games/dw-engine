#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 48;

} // namespace

// Routine 48: the save menu; the section waits until it is closed (script_interp.c,
// tickSaveMachine). The menu is the app's.
core::Result<void> registerSaveMenuRoutine(StoryScenesContext& scenes) {
  return registerScreenRoutine(scenes, Routine, StoryScreen::SaveMenu, true);
}

} // namespace dwe::session
