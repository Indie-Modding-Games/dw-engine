#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 9;

} // namespace

// Routine 9: the jukebox (shop/script_menu.c:947 openJukebox).
core::Result<void> registerJukeboxRoutine(StoryScenesContext& scenes) {
  return registerScreenRoutine(scenes, Routine, StoryScreen::Jukebox, true);
}

} // namespace dwe::session
