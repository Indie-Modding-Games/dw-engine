#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/inventory/inventory.hpp>

#include <cstdint>

// The inventory's state as YAML, for its save section: the session wraps these in a game/save SectionSerializer,
// which stores the version next to the state. Like the original's save (main_menu.c: writeSavegame): the 30 slots
// with their item and count, how many slots the bag has, and the money.
namespace dwe::game::inventory {

// The version of the state that toYaml writes; a change of layout adds a migration in the session.
inline constexpr std::int32_t SectionVersion = 1;

// The state: slot_count, money and slots, each {item, count} or null.
core::YamlNode toYaml(const Inventory& inventory);

// Replaces the inventory's state with what toYaml wrote; the inventory is unchanged on error.
core::Result<void> fromYaml(Inventory& inventory, const core::YamlNode& node);

} // namespace dwe::game::inventory
