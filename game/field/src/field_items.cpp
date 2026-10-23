#include <dwe/core/log.hpp>
#include <dwe/game/field/field_items.hpp>

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace dwe::game::field {

namespace {

// The first free slot of a list that may hold `limit`, growing it up to the limit.
template <typename T>
std::optional<std::size_t> freeSlot(std::vector<std::optional<T>>& slots, std::int32_t limit) {
  const auto found =
      std::ranges::find_if(slots, [](const std::optional<T>& slot) { return !slot.has_value(); });
  if (found != slots.end()) {
    return static_cast<std::size_t>(found - slots.begin());
  }
  if (std::cmp_greater_equal(slots.size(), std::max(limit, 0))) {
    return std::nullopt;
  }
  slots.emplace_back();
  return slots.size() - 1;
}

} // namespace

bool FieldItems::place(GroundItem item) {
  const std::optional<std::size_t> slot = freeSlot(ground_, rules_.groundSlots);
  if (!slot) {
    core::logDebug("[port:field_items] spawnItem (item.c:2437): the ground's {} slots are full",
                   rules_.groundSlots);
    return false;
  }
  core::logDebug("[port:field_items] spawnItem (item.c:2437): slot {} cell ({}, {})", *slot,
                 item.cell.column, item.cell.row);
  ground_[*slot] = std::move(item);
  return true;
}

bool FieldItems::spawn(assets::ItemHandle item, assets::CollisionCell cell, float height) {
  glm::vec3 position = assets::collisionCellCenter(cell);
  position.y = height;
  return place({.item = item, .cell = cell, .position = position});
}

bool FieldItems::drop(assets::ItemHandle item, glm::vec3 position) {
  // spawnDroppedItems lays the item on the ground (y 0) where the enemy stood.
  position.y = 0.0F;
  return place({.item = item, .cell = assets::collisionCellAt(position), .position = position});
}

bool FieldItems::spawnChest(const Chest& chest) {
  const std::optional<std::size_t> slot = freeSlot(chests_, rules_.chestSlots);
  if (!slot) {
    core::logDebug("[port:field_items] spawnChest (map_object.c:841): the {} chest slots are full",
                   rules_.chestSlots);
    return false;
  }
  core::logDebug("[port:field_items] spawnChest (map_object.c:841): slot {} trigger {} taken {}",
                 *slot, chest.trigger, chest.taken);
  chests_[*slot] = chest;
  return true;
}

bool FieldItems::inReach(assets::CollisionCell player, assets::CollisionCell item) const {
  return std::abs(player.column - item.column) <= rules_.reachCells &&
         std::abs(player.row - item.row) <= rules_.reachCells;
}

std::optional<std::size_t> FieldItems::offerPickup(assets::CollisionCell cell) {
  const auto found = std::ranges::find_if(ground_, [&](const std::optional<GroundItem>& item) {
    return item && inReach(cell, item->cell);
  });
  if (found == ground_.end()) {
    offered_ = false;
    return std::nullopt;
  }
  if (std::exchange(offered_, true)) {
    return std::nullopt;
  }
  const auto slot = static_cast<std::size_t>(found - ground_.begin());
  core::logDebug("[port:field_items] checkItemPickup (tamer.c:618): slot {} in reach", slot);
  return slot;
}

std::optional<std::size_t> FieldItems::chestInReach(assets::CollisionCell cell) const {
  for (std::size_t slot = 0; slot < chests_.size(); ++slot) {
    if (chests_[slot] && inReach(cell, assets::collisionCellAt(chests_[slot]->position))) {
      return slot;
    }
  }
  return std::nullopt;
}

void FieldItems::remove(std::size_t slot) {
  if (slot < ground_.size()) {
    core::logDebug("[port:field_items] deleteDroppedItem (item.c:2458): slot {}", slot);
    ground_[slot].reset();
  }
}

void FieldItems::markTaken(std::size_t chest) {
  if (chest < chests_.size() && chests_[chest]) {
    core::logDebug("[port:field_items] tamerTickTakeChest (tamer.c:1565): chest {} taken", chest);
    chests_[chest]->taken = true;
  }
}

void FieldItems::enterMap(std::string_view mapId) {
  if (mapId != mapId_) {
    mapId_ = mapId;
    clear();
  }
}

void FieldItems::clear() {
  core::logDebug("[port:field_items] changeMap (map.c:5335): ground and chests cleared");
  ground_.clear();
  chests_.clear();
  offered_ = false;
}

} // namespace dwe::game::field
