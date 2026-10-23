#include <dwe/core/log.hpp>

#include <memory>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

// script_interp.c: tickScript ticks the routine from the frame after the order until its menu or
// scene says it is done; the app's closing the screen is that.
class ScreenTask final : public game::story::NextFrameTask {
public:
  explicit ScreenTask(StoryScenesContext& scenes) : scenes_{&scenes} {}

protected:
  game::story::TaskStep step() override {
    if (scenes_->screenOpen) {
      return game::story::TaskRunning{};
    }
    return game::story::TaskDone{};
  }

private:
  StoryScenesContext* scenes_;
};

} // namespace

game::story::RoutineStart StoryScenesContext::openScreen(StoryScreenRequest request) {
  core::logDebug("[port:story] call_routine {}: screen {} ({}, {}), waits={}", request.routine,
                 static_cast<int>(request.screen), request.first, request.second, request.waits);
  screenRequests.push_back(request);
  if (!request.waits) {
    return game::story::RoutineGoesOn{};
  }
  screenOpen = true;
  return game::story::RoutineWaits{.task = std::make_shared<ScreenTask>(*this)};
}

core::Result<void> registerScreenRoutine(StoryScenesContext& scenes,
                                         game::story::RoutineNumber number, StoryScreen screen,
                                         bool waits) {
  return scenes.routines.registerRoutine(
      number,
      [&scenes, screen,
       waits](game::story::RoutineNumber routine) -> core::Result<game::story::RoutineStart> {
        return scenes.openScreen({.screen = screen,
                                  .routine = routine,
                                  .first = scenes.operand(),
                                  .second = 0,
                                  .waits = waits});
      });
}

} // namespace dwe::session
