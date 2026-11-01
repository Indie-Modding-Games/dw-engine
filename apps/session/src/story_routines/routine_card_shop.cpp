#include <dwe/core/log.hpp>
#include <dwe/game/story/dialogue.hpp>
#include <dwe/session/card_shop.hpp>
#include <dwe/session/story_adapter.hpp>

#include <memory>
#include <optional>

#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 14;

// rollCardPack (shop/script_menu.c:679): from the frame after the order, each state draws a card
// (rollCard), adds its copy and shows its line with the player as the speaker (showCardTextbox:
// showMapHeadTextbox(line, 0xFD, 0, 0x4d3)); the next state waits until the player closes it. The
// pack's draws done, the routine ends.
class CardPackTask final : public game::story::NextFrameTask {
public:
  CardPackTask(CardShop& shop, StoryAdapter& story) : shop_{&shop}, story_{&story} {}

protected:
  game::story::TaskStep step() override {
    game::story::Dialogue& dialogue = story_->dialogue();
    if (waiting_) {
      if (!dialogue.poll()) {
        return game::story::TaskRunning{};
      }
      waiting_ = false;
    }
    if (drawn_ >= shop_->packDraws()) {
      shop_->close();
      return game::story::TaskDone{};
    }
    ++drawn_;
    const std::optional<PackCard> card = shop_->drawPackCard();
    if (!card) {
      return game::story::TaskRunning{};
    }
    core::logDebug("[port:cards] rollCardPack (shop/script_menu.c:683): draw {} card {} line {}",
                   drawn_, card->card, card->line);
    dialogue.setOwner(game::story::PlayerSpeaker);
    static_cast<void>(dialogue.showText(shop_->lineKey(card->line)));
    waiting_ = true;
    return game::story::TaskRunning{};
  }

private:
  CardShop* shop_;
  StoryAdapter* story_;
  std::int32_t drawn_ = 0;
  bool waiting_ = false;
};

} // namespace

// Routine 14: the card shop (script_interp.c:436). While the selling trigger is set it opens the
// sell menu; otherwise, while the card-buying trigger is set, the buy menu, and else it opens a
// pack (openSellCardMenu, openBuyCardMenu, rollCardPack; shop/script_menu.c:679).
core::Result<void> registerCardShopRoutine(StoryScenesContext& scenes) {
  return scenes.routines.registerRoutine(
      Routine,
      [&scenes](game::story::RoutineNumber routine) -> core::Result<game::story::RoutineStart> {
        if (scenes.trigger(scenes.rules.sellingTrigger)) {
          return scenes.openScreen({.screen = StoryScreen::CardSell,
                                    .routine = routine,
                                    .first = scenes.operand(),
                                    .second = 0,
                                    .waits = true});
        }
        if (scenes.trigger(scenes.rules.buyingCardsTrigger)) {
          return scenes.openScreen({.screen = StoryScreen::CardBuy,
                                    .routine = routine,
                                    .first = scenes.operand(),
                                    .second = 0,
                                    .waits = true});
        }
        CardShop* shop = scenes.desc.cardShop;
        // The bulk purchase (an engine improvement, the bulk_trades option): with money for more
        // than the pack the script has paid, the app asks how many packs and shows their cards.
        if (shop != nullptr && shop->available() && shop->bulkPacks() && shop->packLimit() > 1) {
          return scenes.openScreen({.screen = StoryScreen::CardPacks,
                                    .routine = routine,
                                    .first = scenes.operand(),
                                    .second = 0,
                                    .waits = true});
        }
        if (shop == nullptr || scenes.desc.story == nullptr || !shop->open(CardShopKind::Pack)) {
          core::logDebug("[port:cards] rollCardPack (shop/script_menu.c:679): no card shop; the "
                         "pack is over at once");
          return game::story::RoutineGoesOn{};
        }
        return game::story::RoutineWaits{
            .task = std::make_shared<CardPackTask>(*shop, *scenes.desc.story)};
      });
}

} // namespace dwe::session
