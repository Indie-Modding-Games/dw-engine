#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/card_shop.hpp>
#include <dwe/session/story_scenes.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "discard_screen.hpp"
#include "field_scene.hpp"

namespace dwe::app {

inline constexpr std::string_view CardShopDocument = "card_shop";

struct CardShopScreenDesc {
  // They must outlive the screen; without a shop the story's card menus close at once.
  session::CardShop* shop = nullptr;
  ui::Ui* ui = nullptr;
};

// The card shop's menus the story opens (call_routine 14 with trigger 4 or 3 -> shop/script_menu.c:
// openBuyCardMenu, openSellCardMenu, createCardMenu, tickCardMenu), on the card_shop document: the
// day's stock with its prices and the "is it OK?" box (createSingleCardShopMenu,
// tickSingleCardShop), or the cards held with what the shop pays and the amount box
// (createItemMenuAmountBox, tickItemMenuAmountBox). The keeper talks over the list. A trade closes
// the menu; the field and the game time wait meanwhile.
class CardShopScreen {
public:
  explicit CardShopScreen(CardShopScreenDesc desc);

  void setSpecies(const assets::DigimonTable* species) { species_ = species; }
  // Whether a menu is open: the field, the game time and the game's menu wait meanwhile.
  [[nodiscard]] bool active() const;
  // Takes the story's CardBuy and CardSell screens; false for any other screen.
  bool take(const session::StoryScreenRequest& request, const FieldScene& scene);
  // Takes the document's commands while a menu is open; false for any other command.
  bool handle(const ui::UiCommand& command);
  // Once per engine tick: R1 (walk) held makes up and down jump to the ends; in the amount box the
  // alternative button (alt) held moves by ten and square (extra) takes the most.
  void press(const input::InputState& input);
  // Once per frame: publishes the menu, and tells the story once it closed.
  void update(session::StoryScenes& scenes);

private:
  enum class Phase : std::uint8_t { Closed, Talk, List, Confirm, Amount };

  // A row of the list as the window shows it: the card (none for an empty row), the price shown,
  // the copies (the sell list's count), whether it can be picked and whether the player holds none.
  struct Row {
    std::optional<std::int32_t> card;
    std::int32_t price = 0;
    std::int32_t copies = 0;
    bool choosable = false;
    bool unheld = false;
  };

  void act(std::string_view action);
  void actInList(std::string_view action);
  void actInConfirm(std::string_view action);
  void actInAmount(std::string_view action);
  void pick();
  void close();
  [[nodiscard]] std::vector<Row> rows() const;
  [[nodiscard]] std::optional<Row> current() const;
  [[nodiscard]] std::string cardName(std::int32_t card) const;
  [[nodiscard]] std::int32_t rarityOf(std::int32_t card) const;
  [[nodiscard]] screens::ScreenSettings settings() const;

  CardShopScreenDesc desc_;
  const assets::DigimonTable* species_ = nullptr;
  Phase phase_ = Phase::Closed;
  session::CardShopKind kind_ = session::CardShopKind::Buy;
  // The keeper (variable 254's NPC, by species) and the map, for the trade's sound.
  std::string keeperSpecies_;
  std::string mapId_;
  // createCardMenu's list: six rows shown at a time, as the item menus.
  DiscardListView list_;
  // SHOP_AMOUNT in the "is it OK?" box: 0 yes, 1 no.
  std::int32_t choice_ = 0;
  // SHOP_AMOUNT in the amount box, and the card it sells.
  std::int32_t amount_ = 0;
  std::optional<std::int32_t> picked_;
  bool jumpHeld_ = false;
  bool tenHeld_ = false;
  // Whether the story waits for the menu to close.
  bool waiting_ = false;
};

} // namespace dwe::app
