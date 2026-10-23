#include <dwe/core/log.hpp>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 35;

} // namespace

// Routine 35: the original loads the shops' code from the disc before a shop (script_interp.c:1809
// loadShopLibrary). The engine has it loaded: the section goes on.
core::Result<void> registerShopOverlayRoutine(StoryScenesContext& scenes) {
  return scenes.routines.registerRoutine(
      Routine,
      [](game::story::RoutineNumber /*routine*/) -> core::Result<game::story::RoutineStart> {
        core::logDebug("[port:story] call_routine 35 (script_interp.c:1809): the shops' code is "
                       "always loaded");
        return game::story::RoutineGoesOn{};
      });
}

} // namespace dwe::session
