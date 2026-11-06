#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 54;

} // namespace

// Routine 54 (script_interp.c: call_routine 0x36, script 192 before the farewell): the ending's
// save. gameClearSave finishes the game and opens the save's question (menu 0x45), or nothing
// without a slot; the section waits until it is closed (scriptTickMainMenu). The menu is the app's.
core::Result<void> registerClearSaveRoutine(StoryScenesContext& scenes) {
  return registerScreenRoutine(scenes, Routine, StoryScreen::ClearSave, true);
}

} // namespace dwe::session
