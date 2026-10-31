#include <dwe/core/log.hpp>
#include <dwe/session/story_props.hpp>

#include <array>
#include <utility>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Gearbox = 41;
constexpr game::story::RoutineNumber SelectWithImages = 42;
constexpr game::story::RoutineNumber Boxes = 43;
constexpr game::story::RoutineNumber Select = 44;

// somethingToyTown re-reads the map's background file when box 0 is picked after trigger 350.
constexpr std::int64_t SongHeardTrigger = 350;

} // namespace

// Toy Town's props (script_interp.c: call_routine 0x29-0x2c; map_object.c). Routine 41 puts the
// Robot House's gearbox (omoc03, script 140) and 43 the Toy Mansion's two boxes (omoc08, script
// 145); 42 and 44 pick the box whose lid opens. All four go on in the same frame.
core::Result<void> registerToyTownRoutines(StoryScenesContext& scenes) {
  auto gearbox =
      [&scenes](game::story::RoutineNumber /*routine*/) -> core::Result<game::story::RoutineStart> {
    scenes.propRequests.emplace_back(ToyTownGearboxShown{});
    scenes.propRequests.emplace_back(ToyTownBoxSelected{.box = 0});
    return game::story::RoutineGoesOn{};
  };
  auto boxes =
      [&scenes](game::story::RoutineNumber /*routine*/) -> core::Result<game::story::RoutineStart> {
    scenes.propRequests.emplace_back(ToyTownBoxesShown{});
    return game::story::RoutineGoesOn{};
  };
  auto select =
      [&scenes](game::story::RoutineNumber routine) -> core::Result<game::story::RoutineStart> {
    const std::int32_t box = toyTownBoxOf(scenes.operand());
    if (routine == SelectWithImages && box == 0 && scenes.trigger(SongHeardTrigger)) {
      // readMapTFS(0x92) only reads omoc03's background file into a buffer again; the engine's
      // map images stay loaded, so there is nothing to do.
      core::logDebug("[port:story] somethingToyTown (map_object.c:1156): map 0x92's images are "
                     "already loaded");
    }
    scenes.propRequests.emplace_back(ToyTownBoxSelected{.box = box});
    return game::story::RoutineGoesOn{};
  };
  for (auto [number, handler] :
       std::array<std::pair<game::story::RoutineNumber, game::story::RoutineHandler>, 4>{
           {{Gearbox, gearbox}, {SelectWithImages, select}, {Boxes, boxes}, {Select, select}}}) {
    if (auto added = scenes.routines.registerRoutine(number, std::move(handler)); !added) {
      return added;
    }
  }
  return {};
}

} // namespace dwe::session
