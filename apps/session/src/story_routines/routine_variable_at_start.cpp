#include <dwe/game/story/story_runner.hpp>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 25;

} // namespace

// Routine 25: variable 0 goes back to the operand when the script ends, instead of its value at
// the start (script_interp.c:1775, MAIN_D_80134FE7). The first map's entry section uses it.
core::Result<void> registerVariableAtStartRoutine(StoryScenesContext& scenes) {
  return scenes.routines.registerRoutine(
      Routine,
      [&scenes](game::story::RoutineNumber /*routine*/) -> core::Result<game::story::RoutineStart> {
        scenes.runner().setVariableAtStart(scenes.operand());
        return game::story::RoutineGoesOn{};
      });
}

} // namespace dwe::session
