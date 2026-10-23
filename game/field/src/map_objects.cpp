#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/math.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/field/field.hpp>

#include <cstddef>
#include <optional>

#include "script_messages.hpp"

namespace dwe::game::field {

void Field::placeMapObjects(const assets::MapDoorSet* doors) {
  mapObjects_.clear();
  if (doors == nullptr) {
    return;
  }
  for (const assets::MapDoor& door : doors->doors) {
    if (!door.modelId) {
      mapObjects_.emplace_back(std::nullopt);
      continue;
    }
    mapObjects_.emplace_back(MapObject3D{
        .model = *door.modelId,
        .position = core::fromOriginalAxes(glm::vec3{door.position}),
        .yaw = assets::yawFromOriginal(door.rotation),
    });
  }
  core::logDebug(
      "{}", core::formatMessage(script_messages::MapObjects, doors->index, mapObjects_.size()));
}

bool Field::turnMapObjectByScript(std::int64_t slot, std::int64_t direction) {
  const bool present = slot >= 0 && static_cast<std::size_t>(slot) < mapObjects_.size() &&
                       mapObjects_.at(static_cast<std::size_t>(slot)).has_value();
  if (!present) {
    core::logDebug("{}", core::formatMessage(script_messages::NoMapObject, slot));
    return true;
  }
  core::logDebug("{}", core::formatMessage(script_messages::TurnNotPorted, slot,
                                           mapObjects_.at(static_cast<std::size_t>(slot))->model,
                                           direction));
  return true;
}

} // namespace dwe::game::field
