#include <dwe/session/story_props.hpp>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 34;

} // namespace

// Routine 34: the screen shakes while the section goes on (script_interp.c:1814, map.c:5472
// createMeramonShake): Meramon's tremor in tunn08.
core::Result<void> registerScreenShakeRoutine(StoryScenesContext& scenes) {
  return scenes.routines.registerRoutine(
      Routine,
      [&scenes](game::story::RoutineNumber /*routine*/) -> core::Result<game::story::RoutineStart> {
        scenes.propRequests.emplace_back(ScreenShakeStarted{});
        return game::story::RoutineGoesOn{};
      });
}

} // namespace dwe::session
