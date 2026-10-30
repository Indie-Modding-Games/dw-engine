#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/merit_shop.hpp>
#include <dwe/session/story_scenes.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "discard_screen.hpp"
#include "field_scene.hpp"

namespace dwe::app {

inline constexpr std::string_view MeritShopDocument = "merit_shop";

struct MeritShopScreenDesc {
  // They must outlive the screen; without an exchange the story's routine 1 closes at once.
  session::MeritShop* shop = nullptr;
  // The player's name for the choices.
  const session::GameSession* session = nullptr;
  ui::Ui* ui = nullptr;
};

// The merit exchange the story opens (call_routine 1 -> shop/script_menu.c: openMeritShop,
// createCardMenu, tickCardMenu, createMeritCardTradeDialogue; script_common.c: createItemMenu,
// tickItemMenu, readSelectedItemMerit), on the merit_shop document: the keeper greets, the player
// shows a card, uses the merit points or leaves; a card is traded for its worth after a yes, an
// item taken for its price after a yes, and each trade goes back to the three choices. The points
// box shows from the first choice on. The field and the game time wait meanwhile.
class MeritShopScreen {
public:
  explicit MeritShopScreen(MeritShopScreenDesc desc);

  void setSpecies(const assets::DigimonTable* species) { species_ = species; }
  // Whether a visit is open: the field, the game time and the game's menu wait meanwhile.
  [[nodiscard]] bool active() const;
  // Takes the story's MeritShop screen, with the keeper the operand (variable 254) names on the
  // map; false for any other screen.
  bool take(const session::StoryScreenRequest& request, const FieldScene& scene);
  // Takes the document's commands while a visit is open; false for any other command.
  bool handle(const ui::UiCommand& command);
  // Once per engine tick: R1 (walk) held makes up and down jump to the ends of a list.
  void press(const input::InputState& input);
  // Once per frame: publishes the visit, and tells the story once it closed.
  void update(session::StoryScenes& scenes);

  // openMeritShop's states.
  enum class Phase : std::uint8_t {
    Closed,
    Greeting,   // state 0: line 0
    Choice,     // state 3: the player's three choices, the points box up
    CardList,   // state 4: the card list, line 5 over it
    CardWorth,  // createMeritCardTradeDialogue: the card's worth, the list still up
    CardChoice, // state 7: give it or cancel
    ItemList,   // state 5: the item list, line 6 over it
    ItemChoice, // state 0xb: OK or cancel
  };
  [[nodiscard]] Phase phase() const { return phase_; }

private:
  void act(std::string_view action);
  void actInChoice(std::string_view action);
  void actInList(std::string_view action);
  void pickCard();
  void pickItem();
  void answer(std::int32_t picked);
  void openList(Phase list);
  void close();
  [[nodiscard]] std::int32_t choiceCount() const;
  [[nodiscard]] std::int32_t rowCount() const;
  [[nodiscard]] std::string playerName() const;
  [[nodiscard]] screens::ScreenSettings settings() const;

  MeritShopScreenDesc desc_;
  const assets::DigimonTable* species_ = nullptr;
  Phase phase_ = Phase::Closed;
  // The keeper (variable 254's NPC, by species) and the map, for the trade's sound.
  std::string keeperSpecies_;
  std::string mapId_;
  // The card or item list (createCardMenu, createItemMenu): six rows at a time.
  DiscardListView list_;
  // DIALOGUE_SELECTION.current of the open choice.
  std::int32_t choice_ = 0;
  // SHOP_ITEM_TYPE and SHOP_VARIABLE: the card picked and its worth, or the item picked.
  std::optional<std::int32_t> card_;
  std::int32_t worth_ = 0;
  std::optional<assets::ItemHandle> item_;
  bool jumpHeld_ = false;
  // Whether the story waits for the visit to close.
  bool waiting_ = false;
};

} // namespace dwe::app
