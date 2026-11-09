#include "battle_scene_view.hpp"

#include <dwe/assets/map_document.hpp>
#include <dwe/render/map_camera.hpp>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <utility>

#include "condition_point.hpp"
#include "field_scene.hpp"

namespace dwe::app {

game::battle::EffectView effectViewOf(const render::MapCamera& camera) {
  constexpr float MatrixOne = 4096.0F;
  constexpr std::array<float, 3> Flip{1.0F, -1.0F, -1.0F};
  game::battle::EffectView view;
  const glm::vec2 shown = glm::ceil(camera.visibleSize());
  view.screen = {.x = static_cast<std::int32_t>(shown.x), .y = static_cast<std::int32_t>(shown.y)};
  const glm::mat4& worldToView = camera.view();
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      // glm is column-major: worldToView[column][row].
      const float value =
          Flip.at(row) * Flip.at(column) *
          worldToView[static_cast<glm::length_t>(column)][static_cast<glm::length_t>(row)];
      view.camera.m.at(row).at(column) = static_cast<std::int32_t>(std::lround(value * MatrixOne));
    }
  }
  // glm::mat4's last column is the translation.
  const glm::vec4 translation = worldToView[3];
  view.camera.t = {.x = static_cast<std::int32_t>(std::lround(Flip[0] * translation.x)),
                   .y = static_cast<std::int32_t>(std::lround(Flip[1] * translation.y)),
                   .z = static_cast<std::int32_t>(std::lround(Flip[2] * translation.z))};
  view.projection = static_cast<std::int32_t>(std::lround(camera.desc().focalLength));
  // The projection's centre is the background's: where it is from the middle of what is shown.
  const glm::vec2 centre =
      (camera.desc().backgroundSize * 0.5F) - (camera.scroll() + (camera.visibleSize() * 0.5F));
  view.centre = {.x = static_cast<std::int32_t>(std::lround(centre.x)),
                 .y = static_cast<std::int32_t>(std::lround(centre.y))};
  return view;
}

std::vector<FighterFigure> fighterFiguresOf(const game::battle::BattleState& state,
                                            std::int64_t firstSoundSlot) {
  std::vector<FighterFigure> figures;
  for (const game::battle::Fighter& fighter : state.fighters) {
    figures.push_back(
        {.x = static_cast<float>(fighter.position.x),
         .z = static_cast<float>(fighter.position.z),
         .yaw = assets::yawOfDirection(
             {static_cast<float>(fighter.facing.x), 0.0F, static_cast<float>(fighter.facing.z)}),
         .clip = static_cast<std::uint32_t>(fighter.shown.clip),
         .starts = fighter.shown.starts,
         .frozen = fighter.shown.frozen,
         .soundSlot = firstSoundSlot + static_cast<std::int64_t>(figures.size()),
         .pace = 1.0F,
         .scale = {fighter.shown.flat.across, fighter.shown.flat.up, fighter.shown.flat.across},
         .flatSprite = fighter.shown.flat.sprite});
  }
  return figures;
}

void showBattleEffects(FieldScene& scene, const session::BattleEffects* effects, bool fighting,
                       std::uint64_t frames) {
  std::vector<SceneEffect> shown;
  if (effects != nullptr) {
    for (const game::battle::EffectDraw& draw : effects->draws()) {
      shown.push_back({.move = effects->moveOf(draw.slot),
                       .shape = draw.shape,
                       .palettes = effects->palettesOf(draw.slot)});
    }
  }
  scene.setEffects(std::move(shown), frames);
  scene.setMapLayerHidden(fighting && effects != nullptr && effects->mapHidden());
}

void showBattleStatus(FieldScene& scene, const game::battle::Battle* battle,
                      const assets::DigimonTable* species, std::span<const std::int32_t> enemyNpcs,
                      const ShotColumns* shotColumns) {
  if (battle == nullptr) {
    scene.setStatusVisuals(std::nullopt, {});
    scene.setHitParticles(std::nullopt);
    scene.setFlattenedShots({});
    return;
  }
  const game::battle::BattleState& state = battle->state();
  std::vector<FieldScene::ShotSprite> shots;
  for (const std::optional<game::battle::FlattenedShot>& shot : state.flattenedShots) {
    if (!shot || shot->attacker >= state.fighters.size() || shotColumns == nullptr) {
      continue;
    }
    const std::string_view type =
        battle->rules().effects.originalTypeOf(state.fighters[shot->attacker].combatant.species);
    const auto column = shotColumns->find(type);
    if (column == shotColumns->end()) {
      continue;
    }
    const glm::vec2 facing{static_cast<float>(shot->facing.x), static_cast<float>(shot->facing.z)};
    shots.push_back(
        {.attacker = shot->attacker,
         .at = {static_cast<float>(shot->at.x), static_cast<float>(shot->at.z)},
         .height = static_cast<float>(shot->height),
         .facing = glm::length(facing) > 0.0F ? glm::normalize(facing) : glm::vec2{0.0F, 1.0F},
         .column = column->second});
  }
  scene.setFlattenedShots(std::move(shots));
  std::vector<FieldScene::StatusBody> bodies;
  for (std::size_t id = 0; id < state.fighters.size(); ++id) {
    FieldScene::StatusBody body;
    if (id > 0) {
      if (id > enemyNpcs.size()) {
        break;
      }
      body.npc = enemyNpcs[id - 1];
    }
    const assets::DigimonRow* row =
        species != nullptr ? species->find(state.fighters[id].combatant.species) : nullptr;
    if (row != nullptr) {
      body.height = row->height;
      body.radius = row->radius;
      // translateConditionFXToEntity: the point of its original type (getOriginalType).
      const assets::DigimonRow* point = conditionPointRow(*species, &battle->rules().effects,
                                                          state.fighters[id].combatant.species);
      if (point != nullptr && point->conditionFx) {
        const auto& offset = point->conditionFx->offset;
        body.conditionFx =
            std::pair{point->conditionFx->node,
                      glm::vec3{static_cast<float>(offset[0]), static_cast<float>(offset[1]),
                                static_cast<float>(offset[2])}};
      }
    }
    bodies.push_back(body);
  }
  scene.setStatusVisuals(battle->statusVisuals(), std::move(bodies));
  scene.setHitParticles(battle->hitParticles());
}

} // namespace dwe::app
