#include <dwe/core/log.hpp>
#include <dwe/game/field/field.hpp>

#include <algorithm>
#include <utility>

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

void Field::setCollision(const assets::MapCollision& collision) {
  collision_ = collision;
  extraToiletCells_.clear();
  placeExtraToilets();
  lastTrigger_ = collision_.trigger(assets::originalCellAt(player().position)).value_or(0);
  core::logDebug("field: the collision of {} replaced in place", mapId_);
}

void Field::setExtraToilets(std::vector<ExtraToilet> toilets) {
  clearExtraToilets();
  extraToilets_ = std::move(toilets);
  placeExtraToilets();
}

void Field::setExtraToiletsOn(bool on) {
  if (on == extraToiletsOn_) {
    return;
  }
  extraToiletsOn_ = on;
  if (on) {
    placeExtraToilets();
  } else {
    clearExtraToilets();
  }
}

const ExtraToilet* Field::extraToiletAt(assets::CollisionCell cell) const {
  const auto taken = std::ranges::find(extraToiletCells_, cell,
                                       &std::pair<assets::CollisionCell, std::size_t>::first);
  return taken == extraToiletCells_.end() ? nullptr : &extraToilets_.at(taken->second);
}

void Field::placeExtraToilets() {
  if (!extraToiletsOn_ || !extraToiletCells_.empty()) {
    return;
  }
  // Only free cells take the trigger: a wall, a warp or another trigger stays as the map has it.
  const auto toilet = static_cast<std::uint8_t>(rules_.triggers.toilet);
  for (std::size_t index = 0; index < extraToilets_.size(); ++index) {
    if (extraToilets_[index].map != mapId_) {
      continue;
    }
    for (const assets::CollisionCell cell : extraToilets_[index].cells()) {
      if (assets::MapCollision::contains(cell) &&
          collision_.kind(cell) == assets::CollisionKind::Free) {
        collision_.setValue(cell, toilet);
        extraToiletCells_.emplace_back(cell, index);
      }
    }
    core::logDebug("field: extra toilet {} on {}", index, mapId_);
  }
}

void Field::clearExtraToilets() {
  for (const auto& [cell, index] : extraToiletCells_) {
    collision_.setValue(cell, 0);
  }
  extraToiletCells_.clear();
}

} // namespace dwe::game::field
