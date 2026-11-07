#pragma once

#include <dwe/game/field/field.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/session/field_items_adapter.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/ui/ui.hpp>

#include <algorithm>
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

// TAKE_ITEM_FRAME_COUNTER of the item notices (tamer.c: tamerTickPickupItem, tamerTickTakeChest):
// the original frames since the notice's box opened or since its last confirm, counted on each
// frame the notice waits for a confirm and kept at 10 at most. A confirm is taken once it is past 4
// for an item on the ground and past 5 for a chest.
struct ItemNoticeCounter {
  static constexpr std::int32_t Cap = 10;
  static constexpr std::int32_t GroundAfter = 4;
  static constexpr std::int32_t ChestAfter = 5;

  std::int32_t frames = 0;

  void reset() { frames = 0; }
  void tick() { frames = std::min(frames + 1, Cap); }
  [[nodiscard]] bool takes(bool chest) const { return frames > (chest ? ChestAfter : GroundAfter); }
};

// Picking up the items on the field's ground and taking the chests' items (tamer.c:
// checkItemPickup, tamerTickPickupItem, checkChestCollision, tamerTickTakeChest): the player walks
// onto an item, or confirms next to a chest, a notice names the item, and on confirm it goes to
// the bag; with the bag full a second notice says so and the item stays. The notice holds the
// player while the field runs on (Field::holdForItem). Its box opens the original frame after the
// item is offered, or once a chest's drawer is out, and grows from the player in the original look
// (ui.c: createAnimatedUIBox); it takes a confirm only from TAKE_ITEM_FRAME_COUNTER's fifth frame
// for an item on the ground (a press) and its sixth for a chest (the button held), and a chest
// needs a second confirm once the item is taken. A chest slides its drawer out before its notice
// shows, and back in when the bag is full (map_object.c: tickOpenChestTray, tickCloseChestTray);
// the chests are drawn with their drawers (renderChest).
class FieldItemsScreen {
public:
  // The adapters, the UI and the sound (nullptr: silent) must outlive this object.
  FieldItemsScreen(session::FieldItemsAdapter& items, const session::InventoryAdapter& inventory,
                   ui::Ui& ui, game::story::SoundPort* sound);

  // Whether a notice holds the player.
  [[nodiscard]] bool active() const { return step_ != Step::None; }

  // Before the field ticks, while it is idle: offers the item under the player, or, on `confirm`,
  // the chest in reach, and runs an open notice's original frames by `seconds`. Returns whether it
  // took the confirm, which the field then does not see. While an item on the ground is offered
  // the player holds the ruleset's pickup clip (tamer.c: tamerTickPickupItem state 0:
  // startAnimation 0xc, played on to its last frame through the notice); a chest turns the player
  // to it on clip 0 and stands the partner on its clip 0 (tamerTickTakeChest state 0). The tick
  // after the notice closes gives them and the field back (state 4: tamerSetState(0),
  // partnerSetState(1)).
  bool tick(FieldScene& scene, bool confirm, double seconds);

  // Whether the confirm button is held this frame: a chest's notice reads the button held
  // (tamerTickTakeChest: POLLED_INPUT & CONFIRM_BUTTON).
  void press(const input::InputState& input);

  // Takes the notice's answer and its box's end; false for any other command.
  bool handle(const ui::UiCommand& command);

  // Hands the scene the items on the ground of the map shown, once a frame: those with an icon
  // (ItemEntry::iconPalette) are drawn (item.c: renderDroppedItem), and the chests.
  void show(FieldScene& scene) const;

private:
  // Opening: the chest's drawer slides out before its notice. Taken: the chest's item is in the
  // bag and its notice waits for the confirm that closes it (tamerTickTakeChest state 4).
  enum class Step : std::uint8_t { None, GroundItem, Opening, Chest, Taken, BagFull, Empty };

  void show(Step step, std::string_view text, assets::ItemHandle item);
  // The box shrinks back into the player and the notice lets the player go.
  void close();
  // One original frame of the notice (20 a second).
  void originalFrame();
  // The confirm the notice's step waits for has come.
  void answer();
  [[nodiscard]] bool chestStep() const;
  // Whether the step takes a confirm on this frame of the counter.
  [[nodiscard]] bool ready() const;
  void publishReady();

  session::FieldItemsAdapter* items_;
  const session::InventoryAdapter* inventory_;
  ui::Ui* ui_;
  game::story::SoundPort* sound_;
  Step step_ = Step::None;
  std::size_t slot_ = 0;
  // An item on the ground offered and its box not opened yet (tamerTickPickupItem state 1).
  bool boxDue_ = false;
  // The chest's drawer goes back in (the bag was full); the notice waits for it.
  bool closing_ = false;
  // The notice's box shrinks away; the document goes once it is gone.
  bool boxClosing_ = false;
  ItemNoticeCounter counter_;
  // The time toward the notice's next original frame.
  double frameSeconds_ = 0.0;
  bool confirmHeld_ = false;
  // The answer setting last published (publishReady).
  std::string_view published_;
  // The notice holds the player and the field, and gives them back the tick after it closes.
  game::field::ItemHold hold_ = game::field::ItemHold::None;
  // The player holds a clip of the notice, until the tick after it closes.
  bool playerClipHeld_ = false;
  // The partner stands on its clip 0 for a chest, until the tick after its notice closes.
  bool partnerClipHeld_ = false;
};

} // namespace dwe::app
