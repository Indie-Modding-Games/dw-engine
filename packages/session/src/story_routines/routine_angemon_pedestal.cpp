#include <dwe/core/log.hpp>
#include <dwe/session/story_props.hpp>

#include <memory>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Spawn = 49;
constexpr game::story::RoutineNumber Slide = 50;

// map_object.c: spawnAngemonPedestal (ANGEMON_PEDESTAL_PROGRESS_X/_Z) and moveAngemonPedestal.
constexpr std::int32_t SpawnZ = 1500;
constexpr std::int32_t SlideStep = 10;
constexpr std::int32_t SlideEndZ = 2000;
constexpr std::int32_t Shake = 5;

// One frame of moveAngemonPedestal; true when the pedestal is at its end.
bool slideOneFrame(AngemonPedestalShown& pedestal) {
  pedestal.z += SlideStep;
  pedestal.x = pedestal.z % (2 * SlideStep) == 0 ? Shake : -Shake;
  if (pedestal.z >= SlideEndZ) {
    pedestal.z = SlideEndZ;
    return true;
  }
  return false;
}

// tickScript ticks moveAngemonPedestal once a frame from the frame after the order until it says
// it is done.
class SlideTask final : public game::story::NextFrameTask {
public:
  explicit SlideTask(StoryScenesContext& scenes) : scenes_{&scenes} {}

protected:
  game::story::TaskStep step() override {
    const bool done = slideOneFrame(scenes_->pedestal);
    scenes_->propRequests.emplace_back(scenes_->pedestal);
    if (!done) {
      return game::story::TaskRunning{};
    }
    core::logDebug("[port:story] moveAngemonPedestal (map_object.c:1167): at ({}, 0, {})",
                   scenes_->pedestal.x, scenes_->pedestal.z);
    return game::story::TaskDone{};
  }

private:
  StoryScenesContext* scenes_;
};

} // namespace

// The Angemon shrine's pedestal (script_interp.c: call_routine 0x31 and 0x32; icsa01's script
// 93). Routine 49 puts the pedestal back at its start and goes on; routine 50 slides it a step a
// frame and holds the section until it is at the end: 50 frames from the start.
core::Result<void> registerAngemonPedestalRoutines(StoryScenesContext& scenes) {
  auto spawn =
      [&scenes](game::story::RoutineNumber /*routine*/) -> core::Result<game::story::RoutineStart> {
    scenes.pedestal = AngemonPedestalShown{.x = 0, .z = SpawnZ};
    scenes.propRequests.emplace_back(scenes.pedestal);
    return game::story::RoutineGoesOn{};
  };
  auto slide =
      [&scenes](game::story::RoutineNumber /*routine*/) -> core::Result<game::story::RoutineStart> {
    return game::story::RoutineWaits{.task = std::make_shared<SlideTask>(scenes)};
  };
  if (auto added = scenes.routines.registerRoutine(Spawn, std::move(spawn)); !added) {
    return added;
  }
  return scenes.routines.registerRoutine(Slide, std::move(slide));
}

} // namespace dwe::session
