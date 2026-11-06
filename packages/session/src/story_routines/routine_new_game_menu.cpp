#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 32;

} // namespace

// Routine 32: the opening's questions and the naming of the player and the partner; the section
// waits until they are answered (script_interp.c:512, shop/script_menu.c:1144
// newGameStateMachine). The menus are the app's.
core::Result<void> registerNewGameMenuRoutine(StoryScenesContext& scenes) {
  return registerScreenRoutine(scenes, Routine, StoryScreen::NewGameMenu, true);
}

} // namespace dwe::session
