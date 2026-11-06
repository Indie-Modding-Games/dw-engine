#include <dwe/core/log.hpp>
#include <dwe/session/story_props.hpp>

#include <array>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber SetCart = 19;
constexpr game::story::RoutineNumber ShrinkPile = 20;
constexpr game::story::RoutineNumber LoadCart = 26;
constexpr game::story::RoutineNumber LoadPile = 27;

} // namespace

// The Drill Tunnel's dirt carrying (script_interp.c:1753-1782; map_object.c). Routine 26 loads the
// cart, which shows no model (loadDirtCartModel), and 19 shows the model the operand variable
// picks in front of the partner (setDirtCartModel); 27 loads the pile at its size
// (loadDirtPileModel) and 20 makes it a step smaller (decreaseDirtPileSize). 19 and 20 end the
// frame (longjmp 2), so tunn05's ten shrinks on entering take ten frames; 26 and 27 go on.
core::Result<void> registerDirtCartRoutines(StoryScenesContext& scenes) {
  auto setCart =
      [&scenes](game::story::RoutineNumber /*routine*/) -> core::Result<game::story::RoutineStart> {
    // setDirtCartModel takes the operand as a signed byte.
    const auto model = static_cast<std::int8_t>(static_cast<std::uint8_t>(scenes.operand()));
    scenes.propRequests.emplace_back(DirtCartShown{.model = model});
    return game::story::RoutineYields{};
  };
  auto shrinkPile =
      [&scenes](game::story::RoutineNumber /*routine*/) -> core::Result<game::story::RoutineStart> {
    // DIRT_PILE_SIZE_COUNTER is an int8_t that only goes down.
    scenes.dirtPileSize = static_cast<std::int8_t>(scenes.dirtPileSize - 1);
    core::logDebug("[port:story] decreaseDirtPileSize (map_object.c:1109): size {}",
                   scenes.dirtPileSize);
    scenes.propRequests.emplace_back(DirtPileShown{.size = scenes.dirtPileSize});
    return game::story::RoutineYields{};
  };
  auto loadCart =
      [&scenes](game::story::RoutineNumber /*routine*/) -> core::Result<game::story::RoutineStart> {
    scenes.propRequests.emplace_back(DirtCartShown{.model = DirtCartHidden});
    return game::story::RoutineGoesOn{};
  };
  auto loadPile =
      [&scenes](game::story::RoutineNumber /*routine*/) -> core::Result<game::story::RoutineStart> {
    scenes.propRequests.emplace_back(DirtPileShown{.size = scenes.dirtPileSize});
    return game::story::RoutineGoesOn{};
  };
  for (auto [number, handler] :
       std::array<std::pair<game::story::RoutineNumber, game::story::RoutineHandler>, 4>{
           {{SetCart, setCart},
            {ShrinkPile, shrinkPile},
            {LoadCart, loadCart},
            {LoadPile, loadPile}}}) {
    if (auto added = scenes.routines.registerRoutine(number, std::move(handler)); !added) {
      return added;
    }
  }
  return {};
}

} // namespace dwe::session
