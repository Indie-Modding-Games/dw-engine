#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 11;

} // namespace

// Routine 11: the warehouse that keeps items (shop/script_menu.c:892 tickItemKeeper).
core::Result<void> registerItemKeeperRoutine(StoryScenesContext& scenes) {
  return registerScreenRoutine(scenes, Routine, StoryScreen::ItemKeeper, true);
}

} // namespace dwe::session
