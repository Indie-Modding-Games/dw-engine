#include "field_items_screen.hpp"

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/math.hpp>
#include <dwe/game/field/field_items.hpp>
#include <dwe/screens/name_keys.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "field_scene.hpp"

namespace dwe::app {

namespace {

constexpr std::string_view NoticeAnswer = "field_item_notice_ok";
// The document's box has shrunk away (ui/common/original_box.lua).
constexpr std::string_view ClosedAnswer = "field_item_notice_closed";
// "open" while the notice stands, "closing" once its box shrinks away.
constexpr std::string_view PhaseSetting = "field_item_notice.phase";
// How the document takes a confirm now: "sound", "quiet" (a chest's) or "" (not yet).
constexpr std::string_view AnswerSetting = "field_item_notice.answer";
constexpr std::string_view TextSetting = "field_item_notice.text";
constexpr std::string_view ItemSetting = "field_item_notice.item";
// tamer.c: tamerTickPickupItem and tamerTickTakeChest draw the item's name over the tamer's words
// (tamer_messages: "Woah!", "I can't hold anymore."); an empty chest shows only its own words.
constexpr std::string_view FoundText = "main.tamer_messages.0000";
constexpr std::string_view BagFullText = "main.tamer_messages.0001";
constexpr std::string_view EmptyText = "main.tamer_messages.0002";
// map_object.c: CHEST_TMD and CHEST_SCALE (1).
constexpr std::string_view ChestModel = "model/etcna/taka";
// tamerTickTakeChest state 0: startAnimation(player, 0), and partnerTickIdle's clip 0.
constexpr std::uint32_t StandClip = 0;
// The original's field runs at 20 frames a second (main.c: gameLoop waits VSync(3)).
constexpr double OriginalFrameSeconds = 1.0 / 20.0;

} // namespace

FieldItemsScreen::FieldItemsScreen(session::FieldItemsAdapter& items,
                                   const session::InventoryAdapter& inventory, ui::Ui& ui,
                                   game::story::SoundPort* sound)
    : items_{&items}, inventory_{&inventory}, ui_{&ui}, sound_{sound} {}

bool FieldItemsScreen::tick(FieldScene& scene, bool confirm, double seconds) {
  if (active()) {
    frameSeconds_ += seconds;
    while (active() && frameSeconds_ >= OriginalFrameSeconds) {
      frameSeconds_ -= OriginalFrameSeconds;
      originalFrame();
    }
    return confirm;
  }
  // tamerTickPickupItem and tamerTickTakeChest state 4: tamerSetState(0), and for a chest
  // partnerSetState(1) and setCameraFollowPlayer; the clips of the walk and the follow start on
  // the next frame.
  game::field::Field& field = scene.field();
  if (std::exchange(hold_, game::field::ItemHold::None) != game::field::ItemHold::None) {
    field.holdForItem(game::field::ItemHold::None);
  }
  if (std::exchange(playerClipHeld_, false)) {
    scene.playPlayerSceneClip(std::nullopt);
  }
  if (std::exchange(partnerClipHeld_, false)) {
    scene.playPartnerClip(std::nullopt);
  }
  if (boxClosing_ && ui_->isVisible(FieldItemNoticeDocument)) {
    return confirm;
  }
  boxClosing_ = false;
  game::field::FieldItems& items = items_->items();
  items.enterMap(field.mapId());
  const assets::CollisionCell cell = assets::collisionCellAt(field.player().position);
  if (const std::optional<std::size_t> slot = items.offerPickup(cell)) {
    if (const auto& ground = items.ground()[*slot]) {
      slot_ = *slot;
      // tamerTickPickupItem state 0: the player crouches for the item, the words are written, and
      // the box opens on the next frame (state 1).
      step_ = Step::GroundItem;
      boxDue_ = true;
      frameSeconds_ = 0.0;
      hold_ = game::field::ItemHold::Ground;
      field.holdForItem(hold_);
      if (const auto& rules = items_->rules(); rules && rules->pickupPlayerClip) {
        core::logDebug("[port:field_items] tamerTickPickupItem (tamer.c:1417): startAnimation({})",
                       *rules->pickupPlayerClip);
        scene.playPlayerSceneClip(rules->pickupPlayerClip);
        playerClipHeld_ = true;
      }
    }
    return confirm;
  }
  if (!confirm) {
    return false;
  }
  const std::optional<std::size_t> chest = items.chestInReach(cell);
  if (!chest) {
    return false;
  }
  const auto& inReach = items.chests()[*chest];
  if (!inReach) {
    return false;
  }
  slot_ = *chest;
  core::logDebug("[port:field_items] checkChestCollision (tamer.c:1287): chest {} taken {}", *chest,
                 inReach->taken);
  // tamerTickTakeChest state 0: the player stands on clip 0 turned to the chest, the partner stands
  // on its clip 0 (partnerSetState(0xb): partnerTickIdle), and from the next frame the drawer
  // slides out (state 1); the notice comes once it is out, at once for a taken chest's.
  hold_ = game::field::ItemHold::Chest;
  field.holdForItem(hold_);
  field.turnPlayerTo(game::field::chestPlace(*inReach));
  scene.playPlayerSceneClip(StandClip);
  playerClipHeld_ = true;
  scene.playPartnerClip(StandClip);
  partnerClipHeld_ = true;
  core::logDebug("[port:field_items] tamerTickTakeChest (tamer.c:1510): startAnimation(0), "
                 "partnerSetState(0xb), entityLookAtLocation");
  step_ = Step::Opening;
  closing_ = false;
  frameSeconds_ = 0.0;
  return true;
}

void FieldItemsScreen::press(const input::InputState& input) {
  confirmHeld_ = input.isDown("confirm");
}

void FieldItemsScreen::originalFrame() {
  game::field::FieldItems& items = items_->items();
  if (boxDue_) {
    // tamerTickPickupItem state 1: createAnimatedUIBox, and the counter waits from 0.
    const auto& ground = items.ground()[slot_];
    if (!ground) {
      close();
      return;
    }
    boxDue_ = false;
    counter_.reset();
    show(Step::GroundItem, FoundText, ground->item);
    return;
  }
  if (step_ == Step::Opening) {
    if (!items.slideTrayOut(slot_)) {
      return;
    }
    counter_.reset();
    const auto& chest = items.chests()[slot_];
    if (!chest) {
      close();
    } else if (chest->taken) {
      show(Step::Empty, EmptyText, chest->item);
    } else {
      show(Step::Chest, FoundText, chest->item);
    }
    return;
  }
  if (closing_) {
    // tamerTickTakeChest state 3: the bag was full, the drawer goes back in; the counter waits.
    if (items.slideTrayIn(slot_)) {
      closing_ = false;
    }
    return;
  }
  counter_.tick();
  publishReady();
  // tamerTickTakeChest states 2 and 4 read the button held (POLLED_INPUT): one held from before
  // the box opened answers it as soon as the counter lets it.
  if (chestStep() && confirmHeld_ && ready()) {
    answer();
  }
}

bool FieldItemsScreen::chestStep() const {
  return hold_ == game::field::ItemHold::Chest;
}

bool FieldItemsScreen::ready() const {
  return active() && !boxDue_ && step_ != Step::Opening && !closing_ && counter_.takes(chestStep());
}

void FieldItemsScreen::publishReady() {
  // The document sounds a confirm the notice takes (tamerTickPickupItem: playSound(0, 3)); a
  // chest's takes it silently and a press before the counter lets it is lost.
  std::string_view answer;
  if (ready()) {
    answer = chestStep() ? "quiet" : "sound";
  }
  if (answer != published_) {
    published_ = answer;
    ui_->setSetting(AnswerSetting, std::string{answer});
  }
}

void FieldItemsScreen::show(FieldScene& scene) const {
  std::vector<GroundItemSprite> sprites;
  const assets::ItemTable& table = inventory_->items();
  for (const std::optional<game::field::GroundItem>& ground : items_->items().ground()) {
    if (!ground) {
      continue;
    }
    const assets::ItemEntry& entry = table.at(ground->item);
    if (const auto icon = entry.icon()) {
      sprites.push_back(
          {.position = ground->position, .icon = icon->cell, .palette = icon->palette});
    }
  }
  scene.setGroundItems(std::move(sprites));
  // map_object.c: renderChest draws each chest at its place turned by its rotation, and its drawer
  // (the second object, linked to the chest) moved along the chest's own z (the original's -z is
  // the world's +z: core::fromOriginalAxes).
  std::vector<FieldScene::PropPlace> chests;
  for (const std::optional<game::field::Chest>& chest : items_->items().chests()) {
    if (!chest) {
      continue;
    }
    const float yaw = -static_cast<float>(chest->rotation) * 2.0F * std::numbers::pi_v<float> /
                      static_cast<float>(core::OriginalAnglePerTurn);
    const glm::mat4 world =
        glm::rotate(glm::translate(glm::mat4{1.0F}, game::field::chestPlace(*chest)), yaw,
                    glm::vec3{0.0F, 1.0F, 0.0F});
    const glm::vec3 drawer =
        core::fromOriginalAxes(glm::vec3{0.0F, 0.0F, static_cast<float>(chest->tray)});
    chests.push_back({.world = world,
                      .joints = {glm::mat4{1.0F}, glm::translate(glm::mat4{1.0F}, drawer)},
                      .tint = glm::vec4{1.0F}});
  }
  if (auto model = assets::AssetId::parse(ChestModel)) {
    scene.setPosedProps(*model, std::move(chests));
  }
}

bool FieldItemsScreen::handle(const ui::UiCommand& command) {
  if (command.name == ClosedAnswer) {
    if (boxClosing_) {
      boxClosing_ = false;
      ui_->hide(FieldItemNoticeDocument);
    }
    return true;
  }
  if (command.name != NoticeAnswer) {
    return false;
  }
  if (!ready()) {
    // tamer.c: the confirm counts only past the counter's wait, with the box in place and no
    // drawer moving (tamerTickTakeChest states 1 and 3).
    core::logDebug("[port:field_items] a confirm on the notice's frame {} is not taken",
                   counter_.frames);
    return true;
  }
  answer();
  return true;
}

void FieldItemsScreen::answer() {
  counter_.reset();
  switch (step_) {
  case Step::GroundItem: {
    const session::TakeOutcome outcome = items_->pickUp(slot_);
    if (outcome == session::TakeOutcome::BagFull) {
      // tamerTickPickupItem state 3: the second words, and the counter waits again.
      if (const auto& ground = items_->items().ground()[slot_]) {
        show(Step::BagFull, BagFullText, ground->item);
        publishReady();
        return;
      }
    } else if (const auto& rules = items_->rules();
               outcome == session::TakeOutcome::Taken && rules && sound_ != nullptr) {
      sound_->playSound(rules->pickupSound.slot, rules->pickupSound.sound);
    }
    close();
    return;
  }
  case Step::Chest: {
    // tamerTickTakeChest state 2: the item is the player's (setTrigger) and the notice waits for
    // the confirm that closes it (state 4); with the bag full the drawer goes back in (state 3)
    // under the second words.
    const session::TakeOutcome outcome = items_->takeChest(slot_);
    if (outcome == session::TakeOutcome::Taken) {
      step_ = Step::Taken;
    } else if (const auto& chest = items_->items().chests()[slot_];
               outcome == session::TakeOutcome::BagFull && chest) {
      closing_ = true;
      show(Step::BagFull, BagFullText, chest->item);
    } else {
      close();
      return;
    }
    publishReady();
    return;
  }
  case Step::Taken:
  case Step::BagFull:
  case Step::Empty:
    close();
    return;
  case Step::None:
  case Step::Opening:
    return;
  }
}

void FieldItemsScreen::show(Step step, std::string_view text, assets::ItemHandle item) {
  step_ = step;
  const screens::NameKeys names{nullptr, &inventory_->items(), nullptr};
  ui_->setSetting(ItemSetting, step == Step::Empty ? std::string{} : names.item(item));
  ui_->setSetting(TextSetting, std::string{text});
  ui_->setSetting(PhaseSetting, std::string{"open"});
  ui_->show(FieldItemNoticeDocument, true);
}

void FieldItemsScreen::close() {
  const bool shown = step_ != Step::Opening && !boxDue_;
  step_ = Step::None;
  boxDue_ = false;
  closing_ = false;
  publishReady();
  if (!shown) {
    ui_->hide(FieldItemNoticeDocument);
    return;
  }
  // removeAnimatedUIBox: the box shrinks back into the player while the player walks again; the
  // document sends field_item_notice_closed once it is gone.
  boxClosing_ = true;
  ui_->setSetting(PhaseSetting, std::string{"closing"});
}

} // namespace dwe::app
