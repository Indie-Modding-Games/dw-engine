#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 16;

} // namespace

// Routine 16: the second text box closes and the section waits a frame (script_interp.c:1748,
// script_textbox.c:589 triggerBoxCloseFlag(2)). The text boxes are the dialogue's: the app takes
// the request.
core::Result<void> registerCloseTextBoxRoutine(StoryScenesContext& scenes) {
  return scenes.routines.registerRoutine(
      Routine,
      [&scenes](game::story::RoutineNumber routine) -> core::Result<game::story::RoutineStart> {
        scenes.openScreen({.screen = StoryScreen::CloseTextBox,
                           .routine = routine,
                           .first = 0,
                           .second = 0,
                           .waits = false});
        return game::story::RoutineYields{};
      });
}

} // namespace dwe::session
