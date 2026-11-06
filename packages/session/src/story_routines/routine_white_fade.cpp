#include <dwe/core/log.hpp>
#include <dwe/session/story_props.hpp>

#include <cstdint>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber ToWhite = 45;
constexpr game::story::RoutineNumber FromWhite = 46;

core::Result<void> registerFade(StoryScenesContext& scenes, game::story::RoutineNumber number,
                                bool toWhite) {
  return scenes.routines.registerRoutine(
      number,
      [&scenes,
       toWhite](game::story::RoutineNumber routine) -> core::Result<game::story::RoutineStart> {
        // The length is variable 254, read as a byte (readPStat(0xfe)).
        const auto frames = static_cast<std::int32_t>(scenes.operand() & 0xFF);
        core::logDebug("[port:story] call_routine {}: {}({})", routine,
                       toWhite ? "fadeToWhite" : "fadeFromWhite", frames);
        scenes.propRequests.emplace_back(WhiteFadeStarted{.toWhite = toWhite, .frames = frames});
        return game::story::RoutineGoesOn{};
      });
}

} // namespace

// Routines 45 and 46: the screen fades to white and back while the section goes on
// (script_interp.c:1882, fade.c:170 fadeToWhite and fade.c:193 fadeFromWhite): the Ogre
// Fortress's escape, the angel's shrine and the ending.
core::Result<void> registerWhiteFadeRoutines(StoryScenesContext& scenes) {
  if (auto added = registerFade(scenes, ToWhite, true); !added) {
    return added;
  }
  return registerFade(scenes, FromWhite, false);
}

} // namespace dwe::session
