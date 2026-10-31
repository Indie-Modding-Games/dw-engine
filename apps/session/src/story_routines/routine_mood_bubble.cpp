#include <dwe/core/log.hpp>
#include <dwe/session/story_props.hpp>

#include <cstdint>
#include <optional>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 28;
// readPStat(0xf7) and readPStat(0xf8): the character and its mood.
constexpr std::int64_t CharacterVariable = 0xF7;
constexpr std::int64_t MoodVariable = 0xF8;
// The bubble image has five faces; a higher mood takes the bubble away.
constexpr std::int64_t Moods = 5;

} // namespace

// Routine 28: the mood bubble over the shop rival in the canyon (script_interp.c:1783,
// script_common.c:3038 createMonochromonMoodBubble); the section goes on. Whether the character is
// on the map (scriptIdToEntityId) is the app's to see.
core::Result<void> registerMoodBubbleRoutine(StoryScenesContext& scenes) {
  return scenes.routines.registerRoutine(
      Routine,
      [&scenes](game::story::RoutineNumber /*routine*/) -> core::Result<game::story::RoutineStart> {
        const std::int64_t character = scenes.variable(CharacterVariable) & 0xFF;
        const std::int64_t mood = scenes.variable(MoodVariable) & 0xFF;
        core::logDebug("[port:story] call_routine 28: createMonochromonMoodBubble character {} "
                       "mood {}",
                       character, mood);
        scenes.propRequests.emplace_back(MoodBubbleShown{
            .character = character,
            .mood = mood < Moods ? std::optional{static_cast<std::int32_t>(mood)} : std::nullopt});
        return game::story::RoutineGoesOn{};
      });
}

} // namespace dwe::session
