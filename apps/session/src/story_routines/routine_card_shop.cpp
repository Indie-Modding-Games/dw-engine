#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 14;

} // namespace

// Routine 14: the card shop. While the selling trigger is set it buys cards; otherwise, while the
// card-buying trigger is set, it sells them one by one, and else it sells a pack
// (script_interp.c:439: openSellCardMenu, openBuyCardMenu, rollCardPack; shop/script_menu.c:679).
core::Result<void> registerCardShopRoutine(StoryScenesContext& scenes) {
  return scenes.routines.registerRoutine(
      Routine,
      [&scenes](game::story::RoutineNumber routine) -> core::Result<game::story::RoutineStart> {
        StoryScreen screen = StoryScreen::CardPack;
        if (scenes.trigger(scenes.rules.sellingTrigger)) {
          screen = StoryScreen::CardSell;
        } else if (scenes.trigger(scenes.rules.buyingCardsTrigger)) {
          screen = StoryScreen::CardBuy;
        }
        return scenes.openScreen({.screen = screen,
                                  .routine = routine,
                                  .first = scenes.operand(),
                                  .second = 0,
                                  .waits = true});
      });
}

} // namespace dwe::session
