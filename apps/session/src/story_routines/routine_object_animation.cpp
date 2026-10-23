#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 21;
// The map object and its animation (pstats 247 and 248).
constexpr std::int64_t ObjectVariable = 247;
constexpr std::int64_t AnimationVariable = 248;

} // namespace

// Routine 21: a map object's animation starts over and the section waits a frame
// (script_interp.c:1757, overworld.c:1002 resetMapObjectAnimation). The map's objects are the
// field's: the app takes the request.
core::Result<void> registerObjectAnimationRoutine(StoryScenesContext& scenes) {
  return scenes.routines.registerRoutine(
      Routine,
      [&scenes](game::story::RoutineNumber routine) -> core::Result<game::story::RoutineStart> {
        scenes.openScreen({.screen = StoryScreen::ObjectAnimation,
                           .routine = routine,
                           .first = scenes.variable(ObjectVariable),
                           .second = scenes.variable(AnimationVariable),
                           .waits = false});
        return game::story::RoutineYields{};
      });
}

} // namespace dwe::session
