#include <dwe/core/log.hpp>
#include <dwe/game/field/field.hpp>

namespace dwe::game::field {

void Field::blockCells(assets::CollisionCell from, std::int32_t width, std::int32_t height) {
  // map_collision.c:190 writes the blocked value over whatever each cell held, triggers included.
  for (std::int32_t row = from.row; row < from.row + height; ++row) {
    for (std::int32_t column = from.column; column < from.column + width; ++column) {
      collision_.setValue({.column = column, .row = row}, assets::MapCollision::BlockedBit);
    }
  }
  core::logDebug("[port:field] setImpassableRect (map_collision.c:190): {} x {} cells from ({}, "
                 "{}) on {}",
                 width, height, from.column, from.row, mapId_);
}

} // namespace dwe::game::field
