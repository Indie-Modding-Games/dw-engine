#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/item_table.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/shop_counter.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace dwe::app {

inline constexpr std::string_view ShopMenuDocument = "shop_menu";

struct ShopScreenDesc {
  // They must outlive the screen; without a shop the screen never opens.
  session::GameSession* session = nullptr;
  session::ShopCounter* shop = nullptr;
  ui::Ui* ui = nullptr;
};

// The shop the story opens (script_common.c: openShop), on the shop_menu document: the keeper
// greets, the player buys or sells with the amount box (createItemMenuAmountBox,
// tickItemMenuAmountBox) and leaves; the field and the game time wait meanwhile. In the list, pause
// (Start) opens the description of the item under the cursor (tickItemMenu,
// createItemMenuDescriptionBox). The visit is the session's ShopCounter's; this screen closes it
// when the player leaves. It also publishes what shop extras show (an engine improvement, off
// unless turned on, which the document applies by itself: the shop_extras setting): the focused
// item's description under the list, how many of each the bag holds and the money a trade leaves.
class ShopScreen {
public:
  explicit ShopScreen(ShopScreenDesc desc);

  // The species table, for the descriptions that name a species.
  void setSpecies(const assets::DigimonTable* species) { species_ = species; }

  // Whether a visit is open: the field and the game time wait meanwhile.
  [[nodiscard]] bool active() const;
  // Whether pause belongs to the shop: in the list (it opens an item's description) and while the
  // description is up (it closes it), so the game's pause box does not open over it.
  [[nodiscard]] bool holdsPause() const;
  // Takes the shop menu's commands while a visit is open; false for any other command.
  bool handle(const ui::UiCommand& command);
  // Once per engine tick: pause opens and closes the description box.
  void press(const input::InputState& input);
  // Once per frame: opens the document when the story opened a visit, and publishes the screen.
  void update();

private:
  // openShop's states: what the keeper says, the choice, the item list and the amount box.
  enum class Phase : std::uint8_t { Closed, Talk, Choice, List, Amount, Description };
  enum class Mode : std::uint8_t { Buy, Sell };
  // Where a line goes once the player reads it.
  enum class After : std::uint8_t { Choice, SoldOut, Leave, Close };

  struct Row {
    assets::ItemHandle item;
    std::int32_t price = 0;
    std::int32_t count = 0;
    // How many the bag holds (shop extras).
    std::int32_t owned = 0;
    bool enabled = false;
  };

  void start();
  void talk(std::int32_t line, After after);
  void choose(std::int32_t choice);
  void showList(Mode mode);
  void pick(std::int32_t row);
  void moveAmount(std::int32_t delta);
  void confirm();
  void back();
  void leave();
  void finish();
  void setPhase(Phase phase);
  [[nodiscard]] std::vector<Row> rows() const;
  [[nodiscard]] std::int32_t maxAmount() const;
  [[nodiscard]] screens::ScreenSettings settings() const;

  ShopScreenDesc desc_;
  Phase phase_ = Phase::Closed;
  Mode mode_ = Mode::Buy;
  After after_ = After::Choice;
  std::int32_t line_ = 0;
  // MAIN_D_80134F74: whether the player traded on this visit, for the farewell.
  bool traded_ = false;
  // SHOP_VARIABLE: what the last baby discount saved, for its line.
  std::int32_t saved_ = 0;
  // Whether the money box is up (createShopBitsBox at the first choice, triggerBoxCloseFlag(2) at
  // the farewell).
  bool bitsShown_ = false;
  std::optional<assets::ItemHandle> picked_;
  std::int32_t amount_ = 0;
  // The list row under the cursor (the document's shop_cursor), -1 for none: the item whose
  // description pause opens.
  std::int32_t cursor_ = -1;
  const assets::DigimonTable* species_ = nullptr;
};

} // namespace dwe::app
