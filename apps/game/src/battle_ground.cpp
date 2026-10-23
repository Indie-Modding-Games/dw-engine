#include "battle_ground.hpp"

#include <dwe/game/field/grid.hpp>
#include <dwe/render/map_camera.hpp>

#include <cmath>

#include "field_scene.hpp"

namespace dwe::app {

std::optional<game::battle::ScreenPoint>
FieldBattleGround::screenPointOf(game::battle::Point ground, std::int32_t height) const {
  const render::MapCamera* camera = scene_->camera();
  if (camera == nullptr) {
    return std::nullopt;
  }
  const std::optional<glm::vec2> pixel = scene_->worldToWindow(
      {static_cast<float>(ground.x), static_cast<float>(height), static_cast<float>(ground.z)});
  if (!pixel) {
    return std::nullopt;
  }
  // The original's screen is the 320 x 240 middle of the window, at the camera's scale.
  const glm::vec2 fromCentre = (*pixel - (camera->viewportSize() / 2.0F)) / camera->scale();
  return game::battle::ScreenPoint{.x = static_cast<std::int32_t>(std::lround(fromCentre.x)),
                                   .y = static_cast<std::int32_t>(std::lround(fromCentre.y))};
}

bool FieldBattleGround::blocksStep(game::battle::Point from, game::battle::Point to,
                                   std::int32_t radius) const {
  const game::field::GridMove move = game::field::moveOnGrid(
      scene_->field().collision(), {static_cast<float>(from.x), 0.0F, static_cast<float>(from.z)},
      {static_cast<float>(to.x - from.x), static_cast<float>(to.z - from.z)},
      static_cast<float>(radius));
  return move.blockedX || move.blockedZ;
}

} // namespace dwe::app
