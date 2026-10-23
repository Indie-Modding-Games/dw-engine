#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 15;

} // namespace

// Routine 15: the box with the money, shown while the section goes on (script_common.c:1757
// createShopBitsBox(1)).
core::Result<void> registerMoneyBoxRoutine(StoryScenesContext& scenes) {
  return registerScreenRoutine(scenes, Routine, StoryScreen::MoneyBox, false);
}

} // namespace dwe::session
