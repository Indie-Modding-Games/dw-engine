#include <dwe/core/log.hpp>

#include <cstdint>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr std::int64_t On = 1;
constexpr std::int64_t Off = 0;

bool known(std::int64_t number) {
  return number == static_cast<std::int64_t>(StoryImprovement::AngemonZone);
}

} // namespace

// Routine 261, the engine's own (no routine of the original has the number): a story patch asks
// whether a story improvement is on. It reads the improvement's number from the operand variable
// and answers there, 1 on, 0 off or unknown, so a patch plays the original's section when the
// player turned the option off.
core::Result<void> registerStoryImprovementRoutine(StoryScenesContext& scenes) {
  return scenes.routines.registerRoutine(
      StoryImprovementRoutine,
      [&scenes](game::story::RoutineNumber /*routine*/) -> core::Result<game::story::RoutineStart> {
        const std::int64_t number = scenes.operand();
        const bool on = known(number) &&
                        !scenes.improvementsOff.contains(static_cast<StoryImprovement>(number));
        core::logDebug("[mod:story] routine {}: story improvement {} is {}",
                       StoryImprovementRoutine, number, on ? "on" : "off");
        if (auto written = scenes.state().setVariable(scenes.rules.operandVariable, on ? On : Off);
            !written) {
          return std::unexpected{written.error()};
        }
        return game::story::RoutineGoesOn{};
      });
}

} // namespace dwe::session
