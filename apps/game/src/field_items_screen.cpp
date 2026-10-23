#include "field_items_screen.hpp"

#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/screens/name_keys.hpp>

#include <optional>
#include <string>

namespace dwe::app {

namespace {

constexpr std::string_view NoticeAnswer = "field_item_notice_ok";
constexpr std::string_view TextSetting = "field_item_notice.text";
constexpr std::string_view ItemSetting = "field_item_notice.item";
// tamer.c: tamerTickPickupItem and tamerTickTakeChest draw the item's name over the tamer's words
// (tamer_messages: "Woah!", "I can't hold anymore."); an empty chest shows only its own words.
constexpr std::string_view FoundText = "main.tamer_messages.0000";
constexpr std::string_view BagFullText = "main.tamer_messages.0001";
constexpr std::string_view EmptyText = "main.tamer_messages.0002";

} // namespace

FieldItemsScreen::FieldItemsScreen(session::FieldItemsAdapter& items,
                                   const session::InventoryAdapter& inventory, ui::Ui& ui,
                                   game::story::SoundPort* sound)
    : items_{&items}, inventory_{&inventory}, ui_{&ui}, sound_{sound} {}

bool FieldItemsScreen::tick(const game::field::Field& field, bool confirm) {
  if (active()) {
    return confirm;
  }
  game::field::FieldItems& items = items_->items();
  items.enterMap(field.mapId());
  const assets::CollisionCell cell = assets::collisionCellAt(field.player().position);
  if (const std::optional<std::size_t> slot = items.offerPickup(cell)) {
    if (const auto& ground = items.ground()[*slot]) {
      slot_ = *slot;
      show(Step::GroundItem, FoundText, ground->item);
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
  const game::field::Chest& found = *inReach;
  core::logDebug("[port:field_items] checkChestCollision (tamer.c:1287): chest {} taken {}", *chest,
                 found.taken);
  if (found.taken) {
    show(Step::Empty, EmptyText, found.item);
  } else {
    show(Step::Chest, FoundText, found.item);
  }
  return true;
}

bool FieldItemsScreen::handle(const ui::UiCommand& command) {
  if (command.name != NoticeAnswer || !active()) {
    return false;
  }
  session::TakeOutcome outcome = session::TakeOutcome::Taken;
  if (step_ == Step::GroundItem) {
    outcome = items_->pickUp(slot_);
    const auto& rules = items_->rules();
    if (outcome == session::TakeOutcome::Taken && rules && sound_ != nullptr) {
      sound_->playSound(rules->pickupSound.slot, rules->pickupSound.sound);
    }
  } else if (step_ == Step::Chest) {
    outcome = items_->takeChest(slot_);
  }
  if (outcome == session::TakeOutcome::BagFull) {
    std::optional<assets::ItemHandle> item;
    if (step_ == Step::GroundItem) {
      if (const auto& ground = items_->items().ground()[slot_]) {
        item = ground->item;
      }
    } else if (const auto& chest = items_->items().chests()[slot_]) {
      item = chest->item;
    }
    if (item) {
      show(Step::BagFull, BagFullText, *item);
    } else {
      close();
    }
  } else {
    close();
  }
  return true;
}

void FieldItemsScreen::show(Step step, std::string_view text, assets::ItemHandle item) {
  step_ = step;
  const screens::NameKeys names{nullptr, &inventory_->items(), nullptr};
  ui_->setSetting(ItemSetting, step == Step::Empty ? std::string{} : names.item(item));
  ui_->setSetting(TextSetting, std::string{text});
  ui_->show(FieldItemNoticeDocument, true);
}

void FieldItemsScreen::close() {
  step_ = Step::None;
  ui_->hide(FieldItemNoticeDocument);
}

} // namespace dwe::app
