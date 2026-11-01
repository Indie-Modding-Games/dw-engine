#pragma once

#include <dwe/assets/item_table.hpp>
#include <dwe/assets/map_document.hpp>

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The items lying on the field's ground and the map's chests (item.c: spawnItem,
// spawnDroppedItems, pickupItem; map_object.c: spawnChest; tamer.c: checkItemPickup,
// checkChestCollision). They belong to the map the player is on: a map change clears them
// (map.c: changeMap, map_object.c: unloadMapParts), and the map's scripts spawn them again.
namespace dwe::game::field {

struct FieldItemRules {
  // How many items the ground and the chests hold at once; a spawn past them is lost.
  std::int32_t groundSlots = 0;
  std::int32_t chestSlots = 0;
  // An item or a chest is in reach of the player within this many cells, on each axis.
  std::int32_t reachCells = 0;
};

struct GroundItem {
  assets::ItemHandle item;
  assets::CollisionCell cell;
  glm::vec3 position{};
};

// The chest's drawer (map_object.c: trayLocation.vz, the second object of ETCNA/TAKA.TMD, linked
// to the chest): how far out along the chest's own -z it is, in the original's units. It is shut
// at 0 and all the way out at ChestTrayOut, and slides ChestTrayStep an original frame.
inline constexpr std::int32_t ChestTrayOut = -160;
inline constexpr std::int32_t ChestTrayStep = 20;

struct Chest {
  assets::ItemHandle item;
  glm::vec3 position{};
  // The original's Y rotation, 4096 a turn.
  std::int32_t rotation = 0;
  // The story trigger that marks it taken.
  std::int32_t trigger = 0;
  bool taken = false;
  // The drawer, from 0 (shut) to ChestTrayOut; spawnChest leaves a taken chest's drawer out.
  std::int32_t tray = 0;
};

// Where a chest stands (map_object.c: spawnChest): the centre of the cell under its position, on
// the ground (y 0), whatever point the script gave.
[[nodiscard]] glm::vec3 chestPlace(const Chest& chest);

// The cell a chest is opened from (map_object.c: spawnChest): four cells in front of its own, by
// its rotation (0: four rows on, 0x800: four rows back, 0x400: four columns back, 0xc00: four
// columns on; any other rotation, its own cell). Its own cell and the ones around it are closed
// (setImpassableSquare), so the player reaches it only from the front.
[[nodiscard]] assets::CollisionCell chestReachCell(const Chest& chest);

class FieldItems {
public:
  explicit FieldItems(FieldItemRules rules) : rules_{rules} {}

  [[nodiscard]] const FieldItemRules& rules() const { return rules_; }
  void setRules(FieldItemRules rules) { rules_ = rules; }

  // An item a script lays on a cell (spawnItem), at the player's height; false when the ground is
  // full.
  bool spawn(assets::ItemHandle item, assets::CollisionCell cell, float height);

  // An item dropped at a place, as a beaten enemy's (spawnDroppedItems); false when the ground is
  // full.
  bool drop(assets::ItemHandle item, glm::vec3 position);

  // A chest a script places (spawnChest); false when there is no room for it.
  bool spawnChest(const Chest& chest);

  // The ground items, by slot; an empty slot holds nothing.
  [[nodiscard]] std::span<const std::optional<GroundItem>> ground() const { return ground_; }
  [[nodiscard]] std::span<const std::optional<Chest>> chests() const { return chests_; }

  // The item the player picks up standing on `cell` (checkItemPickup): the first in reach, once;
  // it is offered again only after the player stood out of reach of every item.
  [[nodiscard]] std::optional<std::size_t> offerPickup(assets::CollisionCell cell);

  // The chest in reach of the player standing on `cell` (checkChestCollision): within reachCells of
  // its chestReachCell.
  [[nodiscard]] std::optional<std::size_t> chestInReach(assets::CollisionCell cell) const;

  // Takes an item off the ground (deleteDroppedItem).
  void remove(std::size_t slot);

  // Marks a chest taken.
  void markTaken(std::size_t chest);

  // One original frame of a chest's drawer sliding out (map_object.c: tickOpenChestTray) or back in
  // (tickCloseChestTray); true once it is all the way out or in. A slot without a chest is done.
  bool slideTrayOut(std::size_t chest);
  bool slideTrayIn(std::size_t chest);

  // The map the items belong to: on another map, nothing stays (map.c: changeMap clears them).
  // Who spawns or looks for an item names the map first.
  void enterMap(std::string_view mapId);

  // Nothing stays.
  void clear();

private:
  [[nodiscard]] bool inReach(assets::CollisionCell player, assets::CollisionCell item) const;
  bool place(GroundItem item);

  FieldItemRules rules_;
  std::vector<std::optional<GroundItem>> ground_;
  std::vector<std::optional<Chest>> chests_;
  std::string mapId_;
  // tamer.c: HAS_PICKED_UP_ITEM.
  bool offered_ = false;
};

} // namespace dwe::game::field
