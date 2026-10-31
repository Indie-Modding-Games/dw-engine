#include <dwe/core/log.hpp>

#include <memory>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 53;

// tickScript: from the frame after the order, the routine waits for the ending scene to set the
// scene sync, then leaves the field for the ending (MAIN_STATE 3).
class EndingTask final : public game::story::NextFrameTask {
public:
  explicit EndingTask(StoryScenesContext& scenes) : scenes_{&scenes} {}

protected:
  game::story::TaskStep step() override {
    if (!scenes_->sync.done()) {
      return game::story::TaskRunning{};
    }
    core::logDebug("[port:story] call_routine 53 (script_interp.c:503): the ending scene is "
                   "over, the game ends (MAIN_STATE 3)");
    scenes_->gameEnded = true;
    return game::story::TaskDone{};
  }

private:
  StoryScenesContext* scenes_;
};

} // namespace

// Routine 53 (script_interp.c: call_routine 0x35, script 192): the player's ending scene starts
// (tamerSetState 0x10: tamerTickEnding, the ENDI overlay) with the scene sync cleared, and the
// section waits for its end; then the game closes for the ending movie and the title.
core::Result<void> registerStartEndingRoutine(StoryScenesContext& scenes) {
  return scenes.routines.registerRoutine(
      Routine,
      [&scenes](game::story::RoutineNumber /*routine*/) -> core::Result<game::story::RoutineStart> {
        scenes.sync.begin();
        if (scenes.partnerPort != nullptr) {
          scenes.partnerPort->startScene(game::story::PartnerScene::Ending, std::nullopt);
        } else {
          scenes.sync.finish();
        }
        return game::story::RoutineWaits{.task = std::make_shared<EndingTask>(scenes)};
      });
}

} // namespace dwe::session
