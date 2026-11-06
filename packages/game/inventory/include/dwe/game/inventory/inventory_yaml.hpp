#pragma once

#include <dwe/assets/item_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/inventory/inventory.hpp>

#include <cstdint>
#include <string>
#include <vector>

// The inventory's state as YAML, for its save section: the session wraps these in a
// packages/game/save SectionSerializer, which stores the version next to the state. Like the
// original's save (main_menu.c: writeSavegame): the 30 slots with their item and count, how many
// slots the bag has, and the money.
namespace dwe::game::inventory {

// The version of the state that toYaml writes; a change of layout adds a migration in the session.
// Version 1 named the items by their number in data/items; version 2 names them by id.
inline constexpr std::int32_t SectionVersion = 2;

// The state: slot_count, money and slots, each {item, count} or null, the item by its id in
// `items`.
core::YamlNode toYaml(const Inventory& inventory, const assets::ItemTable& items);

// A stack of a save whose item is in no table mounted (a mod's item, the mod since removed, or a
// number of version 1 that no item of the original has): the save drops it, so a missing mod never
// makes a save unreadable.
struct DroppedStack {
  std::string item;
  std::int32_t count = 0;

  friend bool operator==(const DroppedStack&, const DroppedStack&) = default;
};

// Replaces the inventory's state with what toYaml wrote, the stacks of items that are not in
// `items` left out (their slots empty), and returns those; the inventory is unchanged on error.
core::Result<std::vector<DroppedStack>>
fromYaml(Inventory& inventory, const assets::ItemTable& items, const core::YamlNode& node);

// The step from version 1 to 2: each slot's item number becomes the id of the original's item with
// that number. A number the original does not have stays as it is, which fromYaml drops as an item
// in no table.
core::Result<core::YamlNode> itemNumbersToIds(core::YamlNode state, const assets::ItemTable& items);

} // namespace dwe::game::inventory
