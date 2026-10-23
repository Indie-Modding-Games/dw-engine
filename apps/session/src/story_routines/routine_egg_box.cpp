#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 29;

} // namespace

// Routine 29: the egg box, which waits for the player's input (doo2/doo2.c:721
// DOO2_openEggBox, ticked by DOO2_tickEggInput).
core::Result<void> registerEggBoxRoutine(StoryScenesContext& scenes) {
  return registerScreenRoutine(scenes, Routine, StoryScreen::EggBox, true);
}

} // namespace dwe::session
