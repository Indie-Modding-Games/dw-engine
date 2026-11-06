#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 0;

} // namespace

// Routine 0: the menu to throw an item away (script_common.c:1815 openDiscardItem, ticked by
// script_interp.c:423).
core::Result<void> registerDiscardItemRoutine(StoryScenesContext& scenes) {
  return registerScreenRoutine(scenes, Routine, StoryScreen::DiscardItem, true);
}

} // namespace dwe::session
