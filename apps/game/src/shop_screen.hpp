#pragma once

#include <dwe/assets/item_table.hpp>
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
// tickItemMenuAmountBox) and leaves; the field and the game time wait meanwhile. The visit is the
// session's ShopCounter's; this screen closes it when the player leaves.
class ShopScreen {
public:
  explicit ShopScreen(ShopScreenDesc desc);

  // Whether a visit is open: the field and the game time wait meanwhile.
  [[nodiscard]] bool active() const;
  // Takes the shop menu's commands while a visit is open; false for any other command.
  bool handle(const ui::UiCommand& command);
  // Once per frame: opens the document when the story opened a visit, and publishes the screen.
  void update();

private:
  // openShop's states: what the keeper says, the choice, the item list and the amount box.
  enum class Phase : std::uint8_t { Closed, Talk, Choice, List, Amount };
  enum class Mode : std::uint8_t { Buy, Sell };
  // Where a line goes once the player reads it.
  enum class After : std::uint8_t { Choice, SoldOut, Leave, Close };

  struct Row {
    assets::ItemHandle item;
    std::int32_t price = 0;
    std::int32_t count = 0;
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
  std::optional<assets::ItemHandle> picked_;
  std::int32_t amount_ = 0;
};

} // namespace dwe::app
