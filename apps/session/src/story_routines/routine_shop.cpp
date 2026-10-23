#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 8;

} // namespace

// Routine 8: the shop, or the recycle shop while the selling trigger is set (script_interp.c:430:
// openShop or openRecycleShop, shop/script_menu.c:584).
core::Result<void> registerShopRoutine(StoryScenesContext& scenes) {
  return scenes.routines.registerRoutine(
      Routine,
      [&scenes](game::story::RoutineNumber routine) -> core::Result<game::story::RoutineStart> {
        const bool selling = scenes.trigger(scenes.rules.sellingTrigger);
        return scenes.openScreen({.screen = selling ? StoryScreen::RecycleShop : StoryScreen::Shop,
                                  .routine = routine,
                                  .first = scenes.operand(),
                                  .second = 0,
                                  .waits = true});
      });
}

} // namespace dwe::session
