#include <dwe/game/battle/items.hpp>

#include <cmath>
#include <cstdint>
#include <optional>

#include "battle_screen.hpp"
#include "field_scene.hpp"

// The player's throw in battle and the item on its way to the partner (main.c: startThrowingItem,
// addThrownItem, tickThrownItem, renderThrownItem).
namespace dwe::app {
namespace {

// main.c: startThrowingItem: startAnimation(ENTITY_TABLE[0], 6), the player's throw.
constexpr std::uint32_t PlayerThrowClip = 6;
// main.c: addThrownItem, tickThrownItem: the item sits on the player's node 9 (posData[9]) until it
// leaves the hand.
constexpr std::uint32_t ThrowHandNode = 9;

} // namespace

void BattleScreen::startThrow(assets::ItemHandle item) {
  if (desc_.scene == nullptr) {
    return;
  }
  desc_.scene->playPlayerSceneClip(PlayerThrowClip);
  playerThrowing_ = true;
  const assets::ItemEntry& entry = desc_.inventory->items().at(item);
  if (!entry.iconPalette) {
    return;
  }
  thrown_ = ThrownSprite{.icon = entry.index, .palette = *entry.iconPalette, .flight = {}};
  holdThrown();
  showThrown();
}

void BattleScreen::stepThrow() {
  if (desc_.scene == nullptr) {
    return;
  }
  // The throw's clip played through: back to the battle stance.
  if (playerThrowing_ && desc_.scene->playerClipPlayedThrough()) {
    playerThrowing_ = false;
    standPlayer();
  }
  if (!thrown_ || !battle_ || !battle_->itemInFlight()) {
    thrown_.reset();
    showThrown();
    return;
  }
  const std::optional<std::int32_t> frame = battle_->thrownItemFlightFrame();
  if (frame && thrown_->flight) {
    thrown_->flight =
        game::battle::stepItemFlight(*thrown_->flight, battle_->state().partner().position, *frame);
  } else {
    holdThrown();
  }
  showThrown();
}

void BattleScreen::holdThrown() {
  // addThrownItem, tickThrownItem: in the hand the item follows the node.
  const std::optional<glm::vec3> hand =
      desc_.scene->playerNodePoint(ThrowHandNode, glm::vec3{0.0F});
  if (!thrown_ || !hand) {
    return;
  }
  thrown_->flight =
      game::battle::ItemFlight{.point = {.x = static_cast<std::int32_t>(std::lround(hand->x)),
                                         .z = static_cast<std::int32_t>(std::lround(hand->z))},
                               .height = static_cast<std::int32_t>(std::lround(hand->y))};
}

void BattleScreen::showThrown() const {
  if (!thrown_ || !thrown_->flight) {
    desc_.scene->setFlyingItem(std::nullopt);
    return;
  }
  const game::battle::ItemFlight& flight = *thrown_->flight;
  desc_.scene->setFlyingItem(FlyingItemSprite{.position = {static_cast<float>(flight.point.x),
                                                           static_cast<float>(flight.height),
                                                           static_cast<float>(flight.point.z)},
                                              .icon = thrown_->icon,
                                              .palette = thrown_->palette});
}

void BattleScreen::endThrow() {
  thrown_.reset();
  playerThrowing_ = false;
  if (desc_.scene != nullptr) {
    desc_.scene->setFlyingItem(std::nullopt);
  }
}

} // namespace dwe::app
