#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 38;

} // namespace

// Routine 38: the smoke effect over the map's objects, shown while the section goes on
// (overworld.c:777 createNinjamonEffect).
core::Result<void> registerSmokeEffectRoutine(StoryScenesContext& scenes) {
  return registerScreenRoutine(scenes, Routine, StoryScreen::SmokeEffect, false);
}

} // namespace dwe::session
