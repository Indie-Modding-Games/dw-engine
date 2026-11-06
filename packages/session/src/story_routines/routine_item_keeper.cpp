#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 11;

} // namespace

// Routine 11: the item keeper (shop/script_menu.c:892 tickItemKeeper). The game opens its screen
// (apps/game/src/item_keeper_screen.cpp) on the keeper the routine names (variable 254) and the
// script waits until the visit is over; session/item_keeper.cpp keeps what it holds.
core::Result<void> registerItemKeeperRoutine(StoryScenesContext& scenes) {
  return registerScreenRoutine(scenes, Routine, StoryScreen::ItemKeeper, true);
}

} // namespace dwe::session
