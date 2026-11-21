#include <dwe/game/battle/items.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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
constexpr std::int32_t LandedThrowFrames = 20;

} // namespace

void BattleScreen::startThrow(assets::ItemHandle item, game::battle::FighterId thrower,
                              std::optional<game::coop::PlayerId> hand) {
  if (desc_.scene == nullptr) {
    return;
  }
  if (!hand) {
    desc_.scene->playPlayerSceneClip(PlayerThrowClip);
    playerThrowing_ = true;
  } else if (desc_.scene->playCoopPlayerClip(*hand, PlayerThrowClip) &&
             !std::ranges::contains(coopThrowing_, *hand)) {
    // Co-op: the other player's body throws on this game as this player's own does.
    coopThrowing_.push_back(*hand);
  }
  const assets::ItemEntry& entry = desc_.inventory->items().at(item);
  const std::optional<assets::ItemEntry::Icon> icon = entry.icon();
  if (!icon) {
    return;
  }
  thrown_ = ThrownSprite{.icon = icon->cell,
                         .palette = icon->palette,
                         .flight = {},
                         .thrower = thrower,
                         .hand = hand,
                         .serial = ++throwSerial_,
                         .item = entry.id,
                         .target = std::nullopt};
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
  std::erase_if(coopThrowing_, [this](game::coop::PlayerId id) {
    if (!desc_.scene->coopPlayerClipPlayedThrough(id)) {
      return false;
    }
    static_cast<void>(desc_.scene->playCoopPlayerClip(id, std::nullopt));
    return true;
  });
  for (LandedThrow& landed : landedThrows_) {
    --landed.framesLeft;
  }
  std::erase_if(landedThrows_, [](const LandedThrow& landed) { return landed.framesLeft <= 0; });
  // main.c: tickThrownItem: once the item reached the partner, removeTamerItem drops it
  // (TAMER_ITEM back to 0xFF).
  if (!thrown_ || !battle_ || !battle_->thrownItemOf(thrown_->thrower)) {
    if (thrown_ && battle_ && thrown_->target) {
      landedThrows_.push_back(
          {.thrown = {.serial = thrown_->serial,
                      .thrower = thrown_->hand.value_or(game::coop::HostPlayerId),
                      .item = thrown_->item,
                      .stage = game::coop::BattleThrowStage::Landed,
                      .x = 0,
                      .z = 0,
                      .height = 0,
                      .target = static_cast<std::uint8_t>(*thrown_->target)},
           .framesLeft = LandedThrowFrames});
    }
    thrown_.reset();
    showThrown();
    return;
  }
  const game::battle::FighterId target = battle_->thrownItemOf(thrown_->thrower)->target;
  thrown_->target = target;
  const std::optional<std::int32_t> frame = battle_->thrownItemFlightFrame(thrown_->thrower);
  if (frame && thrown_->flight) {
    thrown_->flight = game::battle::stepItemFlight(
        *thrown_->flight, battle_->state().fighters.at(target).position, *frame);
  } else {
    holdThrown();
  }
  showThrown();
}

void BattleScreen::holdThrown() {
  // addThrownItem, tickThrownItem: in the hand the item follows the node.
  if (!thrown_) {
    return;
  }
  const std::optional<glm::vec3> hand =
      thrown_->hand
          ? desc_.scene->coopPlayerNodePoint(*thrown_->hand, ThrowHandNode, glm::vec3{0.0F})
          : desc_.scene->playerNodePoint(ThrowHandNode, glm::vec3{0.0F});
  if (!hand) {
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

std::vector<game::coop::BattleThrow> BattleScreen::coopThrows() const {
  std::vector<game::coop::BattleThrow> throws;
  for (const LandedThrow& landed : landedThrows_) {
    if (throws.size() + 1 < game::coop::MaxBattleThrows) {
      throws.push_back(landed.thrown);
    }
  }
  if (thrown_ && !thrown_->item.empty()) {
    game::coop::BattleThrow shown{
        .serial = thrown_->serial,
        .thrower = thrown_->hand.value_or(game::coop::HostPlayerId),
        .item = thrown_->item,
        .stage = game::coop::BattleThrowStage::Hand,
        .x = 0,
        .z = 0,
        .height = 0,
        .target = static_cast<std::uint8_t>(thrown_->target.value_or(game::battle::PartnerId))};
    if (thrown_->flight && battle_ && battle_->thrownItemFlightFrame(thrown_->thrower)) {
      shown.stage = game::coop::BattleThrowStage::Flight;
      shown.x = thrown_->flight->point.x;
      shown.z = thrown_->flight->point.z;
      shown.height = thrown_->flight->height;
    }
    throws.push_back(std::move(shown));
  }
  return throws;
}

// The battle's end drops a thrown item still on its way (item.c: removeTamerItem).
void BattleScreen::endThrow() {
  thrown_.reset();
  landedThrows_.clear();
  playerThrowing_ = false;
  if (desc_.scene != nullptr) {
    desc_.scene->setFlyingItem(std::nullopt);
    for (const game::coop::PlayerId id : coopThrowing_) {
      static_cast<void>(desc_.scene->playCoopPlayerClip(id, std::nullopt));
    }
  }
  coopThrowing_.clear();
}

} // namespace dwe::app
