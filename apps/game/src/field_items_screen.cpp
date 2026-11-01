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
constexpr std::string_view TextSetting = "field_item_notice.text";
constexpr std::string_view ItemSetting = "field_item_notice.item";
// tamer.c: tamerTickPickupItem and tamerTickTakeChest draw the item's name over the tamer's words
// (tamer_messages: "Woah!", "I can't hold anymore."); an empty chest shows only its own words.
constexpr std::string_view FoundText = "main.tamer_messages.0000";
constexpr std::string_view BagFullText = "main.tamer_messages.0001";
constexpr std::string_view EmptyText = "main.tamer_messages.0002";
// map_object.c: CHEST_TMD and CHEST_SCALE (1).
constexpr std::string_view ChestModel = "model/etcna/taka";
// The original's field runs at 20 frames a second (main.c: gameLoop waits VSync(3)).
constexpr double OriginalFrameSeconds = 1.0 / 20.0;

} // namespace

FieldItemsScreen::FieldItemsScreen(session::FieldItemsAdapter& items,
                                   const session::InventoryAdapter& inventory, ui::Ui& ui,
                                   game::story::SoundPort* sound)
    : items_{&items}, inventory_{&inventory}, ui_{&ui}, sound_{sound} {}

bool FieldItemsScreen::tick(const game::field::Field& field, bool confirm, double seconds) {
  if (active()) {
    slideTray(seconds);
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
  core::logDebug("[port:field_items] checkChestCollision (tamer.c:1287): chest {} taken {}", *chest,
                 inReach->taken);
  // tamerTickTakeChest: the notice comes once tickOpenChestTray has the drawer out; a taken
  // chest's is out already, so it comes on the first frame.
  step_ = Step::Opening;
  closing_ = false;
  traySeconds_ = 0.0;
  slideTray(OriginalFrameSeconds);
  return true;
}

void FieldItemsScreen::slideTray(double seconds) {
  if (step_ != Step::Opening && !closing_) {
    return;
  }
  game::field::FieldItems& items = items_->items();
  traySeconds_ += seconds;
  while (traySeconds_ >= OriginalFrameSeconds) {
    traySeconds_ -= OriginalFrameSeconds;
    if (closing_) {
      // tamerTickTakeChest state 3: the bag was full, the drawer goes back in.
      if (items.slideTrayIn(slot_)) {
        closing_ = false;
        return;
      }
      continue;
    }
    if (!items.slideTrayOut(slot_)) {
      continue;
    }
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
}

void FieldItemsScreen::show(FieldScene& scene) const {
  std::vector<GroundItemSprite> sprites;
  const assets::ItemTable& table = inventory_->items();
  for (const std::optional<game::field::GroundItem>& ground : items_->items().ground()) {
    if (!ground) {
      continue;
    }
    const assets::ItemEntry& entry = table.at(ground->item);
    if (entry.iconPalette && entry.originalIndex) {
      sprites.push_back(
          {.position = ground->position, .icon = entry.index, .palette = *entry.iconPalette});
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
  if (command.name != NoticeAnswer || !active()) {
    return false;
  }
  if (closing_ || step_ == Step::Opening) {
    // tamerTickTakeChest states 1 and 3: the answer waits for the drawer.
    return true;
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
      // tamerTickTakeChest state 3: a chest's drawer goes back in under the new words.
      closing_ = step_ == Step::Chest;
      traySeconds_ = 0.0;
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
