#pragma once

#include <dwe/game/field/field.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/session/field_items_adapter.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/ui/ui.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace dwe::app {

class FieldScene;

inline constexpr std::string_view FieldItemNoticeDocument = "field_item_notice";
// The console's and the scripts' drop_item("<item id>"): lays an item on the ground at the player's
// feet as a beaten enemy's drop (item.c: spawnDroppedItems), for the autotest routes and to look at
// the ground items without a battle.
inline constexpr std::string_view DropItemCommand = "drop_item";

// Picking up the items on the field's ground and taking the chests' items (tamer.c:
// checkItemPickup, tamerTickPickupItem, checkChestCollision, tamerTickTakeChest): the player walks
// onto an item, or confirms next to a chest, a notice names the item, and on confirm it goes to
// the bag; with the bag full a second notice says so and the item stays. The field waits meanwhile.
// A chest slides its drawer out before its notice shows, and back in when the bag is full
// (map_object.c: tickOpenChestTray, tickCloseChestTray); the chests are drawn with their drawers
// (renderChest).
class FieldItemsScreen {
public:
  // The adapters, the UI and the sound (nullptr: silent) must outlive this object.
  FieldItemsScreen(session::FieldItemsAdapter& items, const session::InventoryAdapter& inventory,
                   ui::Ui& ui, game::story::SoundPort* sound);

  // Whether a notice is on screen: the field waits meanwhile.
  [[nodiscard]] bool active() const { return step_ != Step::None; }

  // Before the field ticks, while it is idle: offers the item under the player, or, on `confirm`,
  // the chest in reach, and slides an open chest's drawer by `seconds`. Returns whether it took the
  // confirm, which the field then does not see. While an item on the ground is offered the player
  // holds the ruleset's pickup clip (tamer.c: tamerTickPickupItem state 0: startAnimation 0xc,
  // played on to its last frame through the notice), and the tick after the notice closes gives
  // it back to its walk (state 4: tamerSetState(0), whose clip starts on the next frame).
  bool tick(FieldScene& scene, bool confirm, double seconds);

  // Takes the notice's answer; false for any other command.
  bool handle(const ui::UiCommand& command);

  // Hands the scene the items on the ground of the map shown, once a frame: those with an icon
  // (ItemEntry::iconPalette) are drawn (item.c: renderDroppedItem), and the chests.
  void show(FieldScene& scene) const;

private:
  // Opening: the chest's drawer slides out before its notice.
  enum class Step : std::uint8_t { None, GroundItem, Opening, Chest, BagFull, Empty };

  void show(Step step, std::string_view text, assets::ItemHandle item);
  void close();
  // Slides the open chest's drawer by whole original frames of `seconds`.
  void slideTray(double seconds);

  session::FieldItemsAdapter* items_;
  const session::InventoryAdapter* inventory_;
  ui::Ui* ui_;
  game::story::SoundPort* sound_;
  Step step_ = Step::None;
  std::size_t slot_ = 0;
  // The chest's drawer goes back in (the bag was full); the notice waits for it.
  bool closing_ = false;
  // The time toward the drawer's next original frame.
  double traySeconds_ = 0.0;
  // The player holds the pickup clip, until the tick after the notice closes.
  bool pickupClipHeld_ = false;
};

} // namespace dwe::app
