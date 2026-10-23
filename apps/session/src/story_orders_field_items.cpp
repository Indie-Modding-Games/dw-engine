#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/session/field_items_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/story_map_edits.hpp>

#include <array>
#include <cstdint>

#include "story_game_context.hpp"

// The story's orders that lay items and chests on the map the player is on (script_interp.c:
// scriptInstruction64to7E SPAWN_ITEM, SPAWN_CHEST), on the field's items.
namespace dwe::session {

namespace {

using script::ApiValue;

// map_object.c:879 spawnChest closes the cells around its own (map_collision.c:
// setImpassableSquare): `ChestReach` cells to each side of its cell.
constexpr std::int32_t ChestReach = 2;

core::Result<ApiValue> spawnItem(StoryGameContext& context, std::int64_t item, std::int64_t column,
                                 std::int64_t row) {
  const auto place = context.game().currentPlace();
  if (!place || context.desc.fieldItems == nullptr) {
    core::logDebug("[port:story] spawn_item with no field shown is dropped");
    return ApiValue{};
  }
  const assets::CollisionCell cell{.column = static_cast<std::int32_t>(column),
                                   .row = static_cast<std::int32_t>(row)};
  // item.c:2445: at the height the player stands.
  const bool laid = context.desc.fieldItems->spawnItem(place->map, static_cast<std::int32_t>(item),
                                                       cell, place->spawn.position.y);
  core::logDebug("[port:story] spawn_item (script_interp.c:2117, item.c:2437): item {} on ({}, "
                 "{}): {}",
                 item, cell.column, cell.row, laid ? "laid" : "no room or no such item");
  return ApiValue{};
}

core::Result<ApiValue> spawnChest(StoryGameContext& context,
                                  const std::array<std::int64_t, 6>& operands) {
  const auto place = context.game().currentPlace();
  if (!place || context.desc.fieldItems == nullptr) {
    core::logDebug("[port:story] spawn_chest with no field shown is dropped");
    return ApiValue{};
  }
  const auto [item, x, y, z, rotation, trigger] = operands;
  const glm::vec3 position = assets::toWorldPosition(
      {static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), static_cast<std::int32_t>(z)});
  const bool placed = context.desc.fieldItems->spawnChest(
      place->map, static_cast<std::int32_t>(item), position, static_cast<std::int32_t>(rotation),
      static_cast<std::int32_t>(trigger));
  core::logDebug("[port:story] spawn_chest (script_interp.c:2122, map_object.c:841): item {} "
                 "trigger {}: {}",
                 item, trigger, placed ? "placed" : "no room or no such item");
  if (placed) {
    const assets::CollisionCell cell = assets::collisionCellAt(position);
    context.mapEdits.emplace_back(StoryBlockedRect{.column = cell.column - ChestReach,
                                                   .row = cell.row - ChestReach,
                                                   .width = 2 * ChestReach,
                                                   .height = 2 * ChestReach});
  }
  return ApiValue{};
}

} // namespace

core::Result<void> publishFieldItemOrders(script::ApiRegistry& api, StoryGameContext& context) {
  namespace names = story_game_api;
  StoryGameContext* game = &context;
  using Operands3 = std::array<std::int64_t, 3>;
  using Operands6 = std::array<std::int64_t, 6>;
  return publishOrders(api, {{names::SpawnItem, integerOrder<3>([game](const Operands3& operands) {
                                return spawnItem(*game, operands[0], operands[1], operands[2]);
                              })},
                             {names::SpawnChest, integerOrder<6>([game](const Operands6& operands) {
                                return spawnChest(*game, operands);
                              })}});
}

} // namespace dwe::session
