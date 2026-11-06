#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 55;

} // namespace

// Routine 55: the animation of the entity the operand names plays once more and stops, and the
// section waits a frame (script_interp.c:1865, overworld.c:1821 setLoopCountToOne). The entities
// are the field's: the app takes the request.
core::Result<void> registerAnimationLoopRoutine(StoryScenesContext& scenes) {
  return scenes.routines.registerRoutine(
      Routine,
      [&scenes](game::story::RoutineNumber routine) -> core::Result<game::story::RoutineStart> {
        scenes.openScreen({.screen = StoryScreen::EndAnimationLoop,
                           .routine = routine,
                           .first = scenes.operand(),
                           .second = 0,
                           .waits = false});
        return game::story::RoutineYields{};
      });
}

} // namespace dwe::session
