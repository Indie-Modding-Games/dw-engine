#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 2;

} // namespace

// Routine 2: the item trader's shop (shop/script_menu.c:1051 openMojyamonShop).
core::Result<void> registerTradeShopRoutine(StoryScenesContext& scenes) {
  return registerScreenRoutine(scenes, Routine, StoryScreen::TradeShop, true);
}

} // namespace dwe::session
