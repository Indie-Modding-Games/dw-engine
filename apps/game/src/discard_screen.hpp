#pragma once

#include <dwe/input/input_state.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/discard_menu.hpp>
#include <dwe/session/story_scenes.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace dwe::app {

inline constexpr std::string_view DiscardMenuDocument = "discard_menu";

// openDiscardItem's item menu (ITEM_MENU_TYPE 5): six rows shown at a time.
inline constexpr std::int32_t DiscardVisibleRows = 6;

// The rows the item menu shows (ItemMenuBox): the first row shown and the cursor among them.
struct DiscardListView {
  std::int32_t top = 0;
  std::int32_t cursor = 0;

  [[nodiscard]] std::int32_t index() const { return top + cursor; }
  // itemMenuCursorUp and itemMenuCursorDown: the cursor stops at the ends and the list scrolls
  // under it at the window's edges.
  void up();
  void down(std::int32_t count);
  // itemMenuCursorTop and itemMenuCursorBottom (R1 held): the first row or the last one.
  void toTop();
  void toBottom(std::int32_t count);
};

struct DiscardScreenDesc {
  // They must outlive the screen; without a menu the story's routine 0 closes at once.
  session::DiscardMenu* menu = nullptr;
  session::InventoryAdapter* inventory = nullptr;
  ui::Ui* ui = nullptr;
};

// The menu the story opens to pick an item from the bag (call_routine 0 -> script_common.c:
// openDiscardItem, tickItemMenu, createSingleCardShopMenu, tickSingleCardShop), on the
// discard_menu document: the bag's slots, the "is it OK?" box with yes and no, and the item's
// description. The pick goes to the story through the session's DiscardMenu; the field and the
// game time wait meanwhile.
class DiscardScreen {
public:
  explicit DiscardScreen(DiscardScreenDesc desc);

  // Whether the menu is open: the field, the game time and the game's menu wait meanwhile.
  [[nodiscard]] bool active() const;
  // Takes the story's DiscardItem screen; false for any other screen.
  bool take(const session::StoryScreenRequest& request);
  // Takes the document's commands while the menu is open; false for any other command.
  bool handle(const ui::UiCommand& command);
  // Once per engine tick: R1 (walk) held makes up and down jump to the ends, and Start (pause)
  // opens the item's description.
  void press(const input::InputState& input);
  // Once per frame: publishes the menu, and tells the story once it closed.
  void update(session::StoryScenes& scenes);

private:
  enum class Phase : std::uint8_t { Closed, List, Confirm, Description };

  void act(std::string_view action);
  void actInList(std::string_view action);
  void close();
  [[nodiscard]] std::int32_t rowCount() const;
  [[nodiscard]] std::optional<session::DiscardRow> current() const;
  [[nodiscard]] screens::ScreenSettings settings() const;

  DiscardScreenDesc desc_;
  Phase phase_ = Phase::Closed;
  DiscardListView list_;
  // SHOP_AMOUNT in the confirm box: 0 yes, 1 no.
  std::int32_t choice_ = 0;
  bool jumpHeld_ = false;
  // Whether the story waits for the menu to close.
  bool waiting_ = false;
};

} // namespace dwe::app
