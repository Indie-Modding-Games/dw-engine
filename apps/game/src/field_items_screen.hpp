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

inline constexpr std::string_view FieldItemNoticeDocument = "field_item_notice";

// Picking up the items on the field's ground and taking the chests' items (tamer.c:
// checkItemPickup, tamerTickPickupItem, checkChestCollision, tamerTickTakeChest): the player walks
// onto an item, or confirms next to a chest, a notice names the item, and on confirm it goes to
// the bag; with the bag full a second notice says so and the item stays. The field waits meanwhile.
class FieldItemsScreen {
public:
  // The adapters, the UI and the sound (nullptr: silent) must outlive this object.
  FieldItemsScreen(session::FieldItemsAdapter& items, const session::InventoryAdapter& inventory,
                   ui::Ui& ui, game::story::SoundPort* sound);

  // Whether a notice is on screen: the field waits meanwhile.
  [[nodiscard]] bool active() const { return step_ != Step::None; }

  // Before the field ticks, while it is idle: offers the item under the player, or, on `confirm`,
  // the chest in reach. Returns whether it took the confirm, which the field then does not see.
  bool tick(const game::field::Field& field, bool confirm);

  // Takes the notice's answer; false for any other command.
  bool handle(const ui::UiCommand& command);

private:
  enum class Step : std::uint8_t { None, GroundItem, Chest, BagFull, Empty };

  void show(Step step, std::string_view text, assets::ItemHandle item);
  void close();

  session::FieldItemsAdapter* items_;
  const session::InventoryAdapter* inventory_;
  ui::Ui* ui_;
  game::story::SoundPort* sound_;
  Step step_ = Step::None;
  std::size_t slot_ = 0;
};

} // namespace dwe::app
