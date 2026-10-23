#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/screens/name_keys.hpp>

#include <optional>
#include <string>

#include "field_items_screen.hpp"

namespace dwe::app {

namespace {

constexpr std::string_view NoticeAnswer = "field_item_notice_ok";
constexpr std::string_view TextSetting = "field_item_notice.text";
constexpr std::string_view ItemSetting = "field_item_notice.item";
constexpr std::string_view FoundText = "field_items.found";
constexpr std::string_view BagFullText = "field_items.bag_full";
constexpr std::string_view EmptyText = "field_items.chest_empty";

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
    slot_ = *slot;
    show(Step::GroundItem, FoundText, items.ground()[*slot]->item);
    return confirm;
  }
  if (!confirm) {
    return false;
  }
  const std::optional<std::size_t> chest = items.chestInReach(cell);
  if (!chest) {
    return false;
  }
  slot_ = *chest;
  const game::field::Chest& found = *items.chests()[*chest];
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
    const std::optional<session::SoundCue> cue = items_->pickupSound();
    if (outcome == session::TakeOutcome::Taken && cue && sound_ != nullptr) {
      sound_->playSound(cue->slot, cue->sound);
    }
  } else if (step_ == Step::Chest) {
    outcome = items_->takeChest(slot_);
  }
  if (outcome == session::TakeOutcome::BagFull) {
    const auto ground = items_->items().ground();
    const auto chests = items_->items().chests();
    const assets::ItemHandle item =
        step_ == Step::GroundItem ? ground[slot_]->item : chests[slot_]->item;
    show(Step::BagFull, BagFullText, item);
  } else {
    close();
  }
  return true;
}

void FieldItemsScreen::show(Step step, std::string_view text, assets::ItemHandle item) {
  step_ = step;
  const screens::NameKeys names{nullptr, &inventory_->items(), nullptr};
  ui_->setSetting(ItemSetting, names.item(item));
  ui_->setSetting(TextSetting, std::string{text});
  ui_->show(FieldItemNoticeDocument, true);
}

void FieldItemsScreen::close() {
  step_ = Step::None;
  ui_->hide(FieldItemNoticeDocument);
}

} // namespace dwe::app
