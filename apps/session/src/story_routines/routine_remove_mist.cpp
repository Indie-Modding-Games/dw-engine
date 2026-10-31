#include <dwe/core/log.hpp>
#include <dwe/session/story_props.hpp>

#include <memory>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 37;

// tickScript ticks tickRemoveMist once a frame from the frame after the order until it says the
// mist is gone.
class RemoveMistTask final : public game::story::NextFrameTask {
public:
  explicit RemoveMistTask(StoryScenesContext& scenes) : scenes_{&scenes} {}

protected:
  game::story::TaskStep step() override {
    ++frames_;
    if (!removeMistOneFrame(scenes_->mist)) {
      return game::story::TaskRunning{};
    }
    core::logDebug("[port:story] tickRemoveMist (overworld.c:1012): the mist is gone after {} "
                   "frame(s)",
                   frames_);
    return game::story::TaskDone{};
  }

private:
  StoryScenesContext* scenes_;
  int frames_ = 0;
};

} // namespace

// Routine 37: the mist of the Misty Trees goes away (mist05, script 115). The section holds until
// tickRemoveMist is done. The script sets trigger 0x94 first and shows text, and renderMist clears
// the mist on every frame drawn with that trigger, so the routine finds it gone and ends on its
// first tick; from the palettes of boot it would take 7 frames.
core::Result<void> registerRemoveMistRoutine(StoryScenesContext& scenes) {
  return scenes.routines.registerRoutine(
      Routine,
      [&scenes](game::story::RoutineNumber /*routine*/) -> core::Result<game::story::RoutineStart> {
        if (scenes.trigger(MistGoneTrigger) && scenes.screens.place().screen != MistKeptScreen) {
          // The frames drawn since the trigger was set (renderMist).
          scenes.mist = {.mist05 = MistClearedMist05, .others = MistClearedOthers};
        }
        return game::story::RoutineWaits{.task = std::make_shared<RemoveMistTask>(scenes)};
      });
}

} // namespace dwe::session
