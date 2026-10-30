#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/item_keeper.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace dwe::app {

inline constexpr std::string_view ItemKeeperDocument = "item_keeper";

// One of the keeper's two lists as the window shows it (ItemMenuBox): the first row shown and the
// cursor's row among the shown ones.
struct KeeperListView {
  std::int32_t top = 0;
  std::int32_t cursor = 0;

  [[nodiscard]] std::int32_t index() const { return top + cursor; }
  // itemMenuCursorUp and itemMenuCursorDown: the cursor stops at the ends; the list scrolls under
  // it at the window's edges. False when it does not move.
  bool up();
  bool down(std::int32_t count);
  // itemMenuCursorMoveToTop and itemMenuCursorMoveToBottom.
  void toTop();
  void toBottom(std::int32_t count);
  // keeperScrollLeftToItem and keeperScrollRightToItem: shows the row `found` (or, when the item
  // is gone, keeps the cursor in the shorter list).
  void scrollTo(std::optional<std::int32_t> found, std::int32_t count, bool keepInList);
};

inline constexpr std::int32_t KeeperVisibleRows = 5;

struct ItemKeeperScreenDesc {
  // They must outlive the screen; without a keeper the screen never opens.
  session::ItemKeeper* keeper = nullptr;
  ui::Ui* ui = nullptr;
};

// The item keeper the story opens (shop/script_menu.c: tickItemKeeper, createItemKeepWindow,
// tickItemKeeperWindow), on the item_keeper document: the keeper asks, the player says yes or no,
// and on yes moves items between the keeper's window (left) and the bag's (right) until cancel;
// the farewell closes the visit. The field and the game time wait meanwhile.
class ItemKeeperScreen {
public:
  explicit ItemKeeperScreen(ItemKeeperScreenDesc desc);

  void setSpecies(const assets::DigimonTable* species) { species_ = species; }
  // Whether a visit is open: the field, the game time and the game's menu wait meanwhile.
  [[nodiscard]] bool active() const;
  // Takes the document's commands while a visit is open; false for any other command.
  bool handle(const ui::UiCommand& command);
  // Once per engine tick: the buttons the menus do not navigate with (move by tens, move all,
  // the item's description, the jump to the ends).
  void press(const input::InputState& input);
  // Once per frame: opens the document when the story opened a visit, and publishes the screen.
  void update();

private:
  enum class Phase : std::uint8_t { Closed, Ask, Choice, Windows, Description, Farewell };

  void act(std::string_view action);
  void actInWindows(std::string_view action);
  void moveRow(session::KeeperAmount amount);
  void openDescription();
  void finish();
  [[nodiscard]] std::int32_t rowCount(session::KeeperSide side) const;
  [[nodiscard]] std::optional<session::KeeperRow> rowAt(session::KeeperSide side,
                                                        std::int32_t index) const;
  [[nodiscard]] KeeperListView& list(session::KeeperSide side);
  [[nodiscard]] screens::ScreenSettings settings() const;

  ItemKeeperScreenDesc desc_;
  const assets::DigimonTable* species_ = nullptr;
  Phase phase_ = Phase::Closed;
  // MAIN_D_80134F90: the window the player works in.
  session::KeeperSide side_ = session::KeeperSide::Kept;
  KeeperListView kept_;
  KeeperListView bag_;
  // The choice's row: 0 yes, 1 no.
  std::int32_t choice_ = 0;
  // R1 (here the walk button) held: up and down jump to the ends.
  bool jumpHeld_ = false;
  // The item whose description is open.
  std::optional<assets::ItemHandle> described_;
};

} // namespace dwe::app
