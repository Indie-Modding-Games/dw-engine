#include "battle_ground.hpp"

#include <dwe/game/field/grid.hpp>
#include <dwe/render/map_camera.hpp>

#include <cmath>

#include "field_scene.hpp"

namespace dwe::app {

namespace {

// The original's screen is the 320 x 240 middle of the window, at the camera's scale.
game::battle::ScreenPoint originalScreenPoint(const render::MapCamera& camera, glm::vec2 pixel) {
  const glm::vec2 fromCentre = (pixel - (camera.viewportSize() / 2.0F)) / camera.scale();
  return {.x = static_cast<std::int32_t>(std::lround(fromCentre.x)),
          .y = static_cast<std::int32_t>(std::lround(fromCentre.y))};
}

} // namespace

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
  return originalScreenPoint(*camera, *pixel);
}

std::optional<game::battle::ScreenPoint>
FighterGround::fighterNodeOnScreen(std::size_t fighter, std::uint32_t node) const {
  const render::MapCamera* camera = scene_->camera();
  if (camera == nullptr || fighter > enemyNpcs_->size()) {
    return std::nullopt;
  }
  const std::optional<std::int32_t> npc =
      fighter == 0 ? std::nullopt : std::optional{(*enemyNpcs_)[fighter - 1]};
  const std::optional<glm::vec2> fraction = scene_->fighterNodeOnWindow(npc, node);
  if (!fraction) {
    return std::nullopt;
  }
  return originalScreenPoint(*camera, *fraction * camera->viewportSize());
}

bool FieldBattleGround::blocksStep(game::battle::Point from, game::battle::Point to,
                                   std::int32_t radius) const {
  // Only the line ahead of where the step ends counts, as in the original: a fighter whose edge
  // already overlaps a blocked cell may still step on to a free one.
  // A stage of its own (a tournament arena) brings its ground.
  const std::optional<assets::MapCollision>& staged = scene_->stage().ground;
  return game::field::stepCollides(
      staged ? *staged : scene_->field().collision(),
      {static_cast<float>(from.x), 0.0F, static_cast<float>(from.z)},
      {static_cast<float>(to.x - from.x), static_cast<float>(to.z - from.z)},
      static_cast<float>(radius));
}

} // namespace dwe::app
