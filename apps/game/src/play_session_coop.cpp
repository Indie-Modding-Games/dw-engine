// The game's part in the co-op battles (docs/architecture/multiplayer.md, §6 decision 3): an engine
// addition, the original has no online play. coop_battles.hpp drives these from the link.
#include <dwe/core/error.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/story_adapter.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "battle_trigger.hpp"
#include "play_session.hpp"

namespace dwe::app {

namespace {

game::battle::Point battlePointOf(glm::vec3 world) {
  return {.x = static_cast<std::int32_t>(std::lround(world.x)),
          .z = static_cast<std::int32_t>(std::lround(world.z))};
}

// inventory.c: selectOptionMenu: an item whose item_color is 1 (battle) or 2 (both) is used in a
// battle.
bool throwable(const assets::ItemEntry& entry) {
  return entry.itemColor == 1 || entry.itemColor == 2;
}

} // namespace

bool PlaySession::coopBattleBlocked() const {
  // A partner on a gym course is busy too: it trains on until its session ends (#365).
  return battleScreen_.active() || screenStops() || storyBusy() || gymScreen_.active() ||
         scene_->field().mapChangePending();
}

std::string PlaySession::trainingCourse() const {
  return gymScreen_.courseId();
}

core::Result<void> PlaySession::startVisitorBattle(game::field::PlayerId owner,
                                                   const std::vector<std::int32_t>& npcs,
                                                   session::VisitingPartner visitor,
                                                   bool fixedFight, bool predefined) {
  if (!species_) {
    return core::fail("no species table for the battle");
  }
  const game::field::Field& field = scene_->field();
  const auto guest = field.player(owner);
  const auto guestPartner = field.partner(owner);
  if (!guest || !guestPartner) {
    return core::fail("the guest or its partner is not in this field");
  }
  std::vector<std::size_t> slots;
  for (const std::int32_t npc : npcs) {
    if (npc >= 0) {
      slots.push_back(static_cast<std::size_t>(npc));
    }
  }
  if (slots.empty()) {
    return core::fail("no enemy of the map's NPCs");
  }
  auto battle = mapBattleRequest(field, scene_->map(), *species_, slots);
  if (!battle) {
    return std::unexpected{battle.error()};
  }
  battle->request.tamer = battlePointOf(guest->position);
  battle->request.partner = battlePointOf(guestPartner->position);
  battle->request.predefined = predefined;
  battle->request.visitor = std::move(visitor);
  scene_->setBattlePartner(owner);
  battleScreen_.setVisitorOwner(owner);
  auto started =
      battleScreen_.start(std::move(battle->request), std::move(battle->enemyNpcs), fixedFight);
  if (!started) {
    scene_->setBattlePartner(std::nullopt);
  }
  return started;
}

session::VisitingPartner PlaySession::coopFighter() const {
  return game_.systems.battle->visitorOf(*game_.session);
}

std::optional<std::int32_t> PlaySession::eventNpc() const {
  return game_.systems.story != nullptr ? game_.systems.story->eventNpc() : std::nullopt;
}

core::Result<void> PlaySession::applyCoopSpoils(const game::battle::BattleResult& result) {
  if (game_.systems.battle == nullptr) {
    return core::fail("no battles in this game");
  }
  auto finished = game_.systems.battle->finish(*game_.session, result);
  if (!finished) {
    return std::unexpected{finished.error()};
  }
  return {};
}

void PlaySession::useCoopBattleSounds(std::span<const std::string> enemies,
                                      std::span<const std::string> partners) {
  if (!enemies.empty()) {
    game_.session->useEnemySounds(enemies);
  }
  game_.session->useAllySounds(partners);
}

std::vector<std::pair<std::string, std::int32_t>> PlaySession::coopThrowableItems() const {
  std::vector<std::pair<std::string, std::int32_t>> items;
  const session::InventoryAdapter* inventory = game_.systems.inventory;
  if (inventory == nullptr) {
    return items;
  }
  for (const auto& stack : inventory->inventory().slots()) {
    if (!stack || stack->count <= 0) {
      continue;
    }
    const assets::ItemEntry& entry = inventory->items().at(stack->item);
    if (throwable(entry)) {
      items.emplace_back(entry.id, stack->count);
    }
  }
  return items;
}

screens::NameKeys PlaySession::coopNameKeys() const {
  const session::InventoryAdapter* inventory = game_.systems.inventory;
  return {species_ ? &*species_ : nullptr, inventory != nullptr ? &inventory->items() : nullptr,
          nullptr};
}

session::InventoryAdapter* PlaySession::coopInventory() {
  return game_.systems.inventory;
}

const session::InventoryAdapter* PlaySession::coopInventory() const {
  return game_.systems.inventory;
}

bool PlaySession::takeCoopItem(std::string_view itemId) {
  session::InventoryAdapter* inventory = game_.systems.inventory;
  if (inventory == nullptr) {
    return false;
  }
  const auto item = inventory->items().handleOf(itemId);
  if (!item) {
    return false;
  }
  for (const auto& stack : inventory->inventory().slots()) {
    if (stack && stack->item == *item && stack->count > 0) {
      inventory->inventory().remove(*item, 1);
      return true;
    }
  }
  return false;
}

} // namespace dwe::app
