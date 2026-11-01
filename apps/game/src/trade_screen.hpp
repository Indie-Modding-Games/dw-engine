#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/item_trader.hpp>
#include <dwe/session/story_scenes.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <string_view>

#include "bulk_amount.hpp"
#include "field_scene.hpp"

namespace dwe::app {

inline constexpr std::string_view TradeMenuDocument = "trade_menu";

// The trade menu's cursor (ItemMenuBox of openMojyamonShop): the rows do not scroll and the
// cursor stops at the ends (itemMenuCursorUp and itemMenuCursorDown).
struct TradeCursor {
  std::int32_t row = 0;

  void up() { row = row > 0 ? row - 1 : 0; }
  void down(std::int32_t count) { row = row + 1 < count ? row + 1 : row; }
};

struct TradeScreenDesc {
  // They must outlive the screen; without a trader the story's routine 2 closes at once.
  session::ItemTrader* trader = nullptr;
  // The player's name for the yes-or-no; the refusal's sound.
  const session::GameSession* session = nullptr;
  game::story::SoundPort* sound = nullptr;
  ui::Ui* ui = nullptr;
};

// The item trader the story opens (call_routine 2 -> shop/script_menu.c: openMojyamonShop,
// createMojyaTradeMenu, tickMojyaTradeMenu, updateMojyaTradeStrings), on the trade_menu document:
// the trader greets and asks, the player picks a row of the table (what the bag gives, what the
// trader gives), the trader asks again and the player says yes or no; the trade or the farewell
// closes the visit. The field and the game time wait meanwhile. With the bulk trades on (an engine
// improvement), an item the bag could trade more than once first asks "How many?"; the yes then
// makes that many single trades.
class TradeScreen {
public:
  explicit TradeScreen(TradeScreenDesc desc);

  void setSpecies(const assets::DigimonTable* species) { species_ = species; }
  // The bulk_trades option (on by default).
  void setBulk(bool enabled) { bulk_ = enabled; }
  // Whether a visit is open: the field, the game time and the game's menu wait meanwhile.
  [[nodiscard]] bool active() const;
  // Takes the story's TradeShop screen, with the trader the operand (variable 254) names on the
  // map; false for any other screen.
  bool take(const session::StoryScreenRequest& request, const FieldScene& scene);
  // Takes the document's commands while a visit is open; false for any other command.
  bool handle(const ui::UiCommand& command);
  // Once per engine tick: R1 (walk) held makes up and down jump to the ends.
  void press(const input::InputState& input);
  // Once per frame: publishes the visit, and tells the story once it closed.
  void update(session::StoryScenes& scenes);

  // The visit's steps (openMojyamonShop's states), for the tests and the log.
  enum class Phase : std::uint8_t {
    Closed,
    Greeting, // state 0: line 8
    List,     // state 3: the menu, line 9 under it
    Confirm,  // tickMojyaTradeMenu's confirm: line 11 with the offered item
    Choice,   // state 4: the yes-or-no
    Thanks,   // state 5: line 13, the trade made
    BagFull,  // state 5: line 14, the bag did not take the item
    Farewell, // state 6: line 10
    Amount,   // the bulk trade's "How many?" (an engine improvement), before line 11
  };
  [[nodiscard]] Phase phase() const { return phase_; }

private:
  void act(std::string_view action);
  void actInList(std::string_view action);
  void actInAmount(std::string_view action);
  void close();
  [[nodiscard]] std::int32_t rowCount() const;
  [[nodiscard]] std::string playerName() const;
  [[nodiscard]] screens::ScreenSettings settings() const;

  TradeScreenDesc desc_;
  const assets::DigimonTable* species_ = nullptr;
  Phase phase_ = Phase::Closed;
  TradeCursor cursor_;
  // DIALOGUE_SELECTION.current: 0 yes, 1 no.
  std::int32_t choice_ = 0;
  bool jumpHeld_ = false;
  bool bulk_ = true;
  // The bulk trade's count while it is picked, and the trades the yes makes.
  BulkAmount amount_;
  std::int32_t count_ = 1;
  // Whether the story waits for the visit to close.
  bool waiting_ = false;
};

} // namespace dwe::app
