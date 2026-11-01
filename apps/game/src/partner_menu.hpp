#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/inventory/item_effect.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/cards_adapter.hpp>
#include <dwe/session/city_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/ui/ui.hpp>

#include <string>
#include <string_view>

#include "feed_scene.hpp"

namespace dwe::app {

// The partner's menu document and the commands it sends.
inline constexpr std::string_view PartnerMenuDocument = "partner_menu";
// The pause menu's entry that opens it.
inline constexpr std::string_view OpenPartnerMenuCommand = "open_partner_menu";
inline constexpr std::string_view BagUseCommand = "bag_use";
inline constexpr std::string_view BagMoveCommand = "bag_move";
inline constexpr std::string_view BagDropCommand = "bag_drop";
inline constexpr std::string_view BagSortCommand = "bag_sort";
// The page the menu opens on ("items" or "status") and where it is open: "battle" for the player's
// bag in battle (battle_tick.c: tamerTickBattle), empty in the field.
inline constexpr std::string_view PartnerMenuStartPageSetting = "partner_menu.start_page";
inline constexpr std::string_view PartnerMenuItemsPage = "items";
inline constexpr std::string_view PartnerMenuContextSetting = "partner_menu.context";
inline constexpr std::string_view PartnerMenuBattleContext = "battle";
// TODO(phase 4): praise and scold are field-only in the original (partner.c:
// partnerTickPraiseScold); remove this tab once the field interaction with the partner exists
// (design owner, 2026-10-21).
inline constexpr std::string_view PraiseCommand = "partner_praise";
inline constexpr std::string_view ScoldCommand = "partner_scold";
// The console's and the scripts' give_item("<item id> <count>"): puts items in the bag, as a debug
// aid until the field's chests and shops exist (the original's new game starts with an empty bag).
inline constexpr std::string_view GiveItemCommand = "give_item";
// The console's and the scripts' use_item("<item id>"): uses one from the bag on the partner, as
// the bag's Use does, for the autotest routes that cannot find its slot.
inline constexpr std::string_view UseItemCommand = "use_item";
// What the last praise or scold was, as a text key (partner_menu.care_result.*); empty before any.
inline constexpr std::string_view CareResultSetting = "partner.care_result";
// How many times the `extra` action (the original's square) was pressed while the menu was open;
// the document reacts to each change (the tech page's change view opens its help page,
// game_menu.c: tickDigimonMenu). The menus' navigation has no such step, so the game passes it on.
inline constexpr std::string_view ExtraPressesSetting = "partner_menu.extra_presses";

struct PartnerMenuDesc {
  session::GameSession* session = nullptr;
  session::InventoryAdapter* inventory = nullptr;
  // For the partner's name; it may be null.
  const assets::DigimonTable* species = nullptr;
  ui::Ui* ui = nullptr;
  session::BattleAdapter* battle = nullptr;
  session::StoryAdapter* story = nullptr;
  // The player's cards, for the player page's card list; it may be null.
  const session::CardsAdapter* cards = nullptr;
  // The city, for the player page's City tab; it may be null.
  const session::CityAdapter* city = nullptr;
  // The field's feeding scene, which the bag's Use starts when it can; null: the bag uses the
  // item at once.
  FeedScene* feed = nullptr;
};

// The partner's menu: its status page, the item bag and the care commands. It publishes the views
// of the partner and the bag (apps/screens) while the document is open and passes the player's
// commands to the session and the inventory adapter, which decide what they do.
class PartnerMenu {
public:
  explicit PartnerMenu(PartnerMenuDesc desc) : desc_(desc) {}

  // Publishes the views while the menu is open; once per frame.
  void update();
  // Where the bag is open: the field's menu, or the battle, which opens the same bag
  // (battle_tick.c: tamerTickBattle calls addInventoryUI) and decides what a battle can use.
  void setBagContext(game::inventory::UseContext context) { bagContext_ = context; }

  // The give_item command: `argument` is an item id and a count.
  core::Result<void> giveItem(std::string_view argument);
  // Uses one of the item on the partner (use_item); what came of it shows as the bag's result and
  // is logged.
  core::Result<void> useItem(std::string_view id);

  // The partner's status as the status page shows it now (the partner.* settings), open or not;
  // empty with no game. The autotest reads it to check what the care commands did.
  [[nodiscard]] screens::ScreenSettings statusSettings() const;

  // Takes the partner menu's commands (and the pause menu's entry to it); false for any other
  // command.
  bool handle(const ui::UiCommand& command);

  // Passes the `extra` presses on to the open menu (ExtraPressesSetting); once per tick.
  void press(const input::InputState& input);

private:
  void open();
  void publish() const;
  void use(double value);
  void feed(assets::ItemHandle item);
  void move(const std::string& text);
  void drop(double value);
  void sort(double value);
  void care(bool scold);
  void bagResult(std::string_view key, std::optional<assets::ItemHandle> item) const;

  PartnerMenuDesc desc_;
  game::inventory::UseContext bagContext_ = game::inventory::UseContext::Field;
  double extraPresses_ = 0.0;
  // Whether the pause menu was open last frame: the City tab is read once each time it opens, since
  // nothing recruits while the game is paused.
  bool pauseMenuOpen_ = false;
};

} // namespace dwe::app
