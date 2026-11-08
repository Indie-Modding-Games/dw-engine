#include <dwe/core/log.hpp>

#include <cstddef>

#include "../story_death.hpp"
#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = EggBoxRoutine;
// DOO2_openEggBox starts on the first egg.
constexpr std::size_t FirstEgg = 0;

} // namespace

// Routine 29: the egg box, which waits for the player's input (doo2/doo2.c:721
// DOO2_openEggBox, ticked by DOO2_tickEggInput). The DOO2 overlay's load (loadDynamicLibrary) has
// no counterpart: the engine has no overlays.
core::Result<void> registerEggBoxRoutine(StoryScenesContext& scenes) {
  return scenes.routines.registerRoutine(
      Routine,
      [&scenes](game::story::RoutineNumber routine) -> core::Result<game::story::RoutineStart> {
        scenes.egg = FirstEgg;
        core::logDebug("[port:death] DOO2_openEggBox (doo2.c:721): the box opens on egg {}",
                       FirstEgg);
        return scenes.openScreen({.screen = StoryScreen::EggBox,
                                  .routine = routine,
                                  .first = scenes.operand(),
                                  .second = 0,
                                  .waits = true});
      });
}

} // namespace dwe::session
