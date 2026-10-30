#include "battle_effect_bodies.hpp"

#include <dwe/assets/map_document.hpp>

#include <glm/ext/matrix_transform.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "battle_motion.hpp"
#include "field_scene.hpp"

namespace dwe::app {

namespace {

using game::battle::EffectMatrix;
using game::battle::EffectVector;

constexpr double Turn = 4096.0;
constexpr std::int32_t AngleMask = 0xFFF;
constexpr float MatrixOne = 4096.0F;

// The console's axes are the world's with y and z the other way (core::fromOriginalAxes).
EffectVector originalOf(glm::vec3 world) {
  return {.x = static_cast<std::int32_t>(std::lround(world.x)),
          .y = static_cast<std::int32_t>(std::lround(-world.y)),
          .z = static_cast<std::int32_t>(std::lround(-world.z))};
}

// rotation.vy of a fighter facing this ground direction: the original's rotation 0 faces its -z,
// the world's +z, and a quarter turn its -x (map_document.hpp: yawFromOriginal).
std::int32_t rotationOf(const game::battle::Facing& facing) {
  if (facing.x == 0 && facing.z == 0) {
    return 0;
  }
  const double angle = std::atan2(-static_cast<double>(facing.x), static_cast<double>(facing.z));
  return static_cast<std::int32_t>(std::lround(angle / (2.0 * std::numbers::pi) * Turn)) &
         AngleMask;
}

// A world matrix in the console's axes and units: F W F with F = diag(1, -1, -1), 4096 for 1.
EffectMatrix originalMatrix(const glm::mat4& world) {
  constexpr std::array<float, 3> Flip{1.0F, -1.0F, -1.0F};
  EffectMatrix out;
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      const float value =
          world[static_cast<glm::length_t>(column)][static_cast<glm::length_t>(row)];
      out.m.at(row).at(column) = static_cast<std::int32_t>(
          std::lround(Flip.at(row) * Flip.at(column) * value * MatrixOne));
    }
  }
  out.t = originalOf(glm::vec3{world[3]});
  return out;
}

} // namespace

std::optional<std::optional<std::int32_t>>
BattleEffectBodies::npcOf(game::battle::FighterId fighter) const {
  if (fighter == 0) {
    return std::optional<std::int32_t>{};
  }
  if (fighter > enemyNpcs_->size()) {
    return std::nullopt;
  }
  return std::optional<std::int32_t>{(*enemyNpcs_)[fighter - 1]};
}

std::optional<game::battle::EffectBody>
BattleEffectBodies::body(game::battle::FighterId fighter) const {
  const game::battle::BattleState& state = battle_->state();
  if (fighter >= state.fighters.size()) {
    return std::nullopt;
  }
  const game::battle::Fighter& shown = state.fighters[fighter];
  game::battle::EffectBody out;
  out.species = std::string{battle_->rules().effects.originalTypeOf(shown.combatant.species)};
  out.location = originalOf(
      {static_cast<float>(shown.position.x), 0.0F, static_cast<float>(shown.position.z)});
  out.rotationY = rotationOf(shown.facing);
  if (const assets::DigimonRow* row =
          species_ != nullptr ? species_->find(shown.combatant.species) : nullptr) {
    out.radius = row->radius;
    out.height = row->height;
  }
  out.standing = !shown.fainted;
  return out;
}

std::optional<EffectMatrix> BattleEffectBodies::node(game::battle::FighterId fighter,
                                                     std::int32_t index) const {
  const std::optional<std::optional<std::int32_t>> npc = npcOf(fighter);
  if (!npc || scene_ == nullptr || index < 0) {
    return std::nullopt;
  }
  const std::optional<glm::mat4> world =
      scene_->fighterNode(*npc, static_cast<std::uint32_t>(index));
  if (!world) {
    return std::nullopt;
  }
  return originalMatrix(*world);
}

std::optional<EffectMatrix> BattleEffectBodies::node(game::battle::FighterId fighter,
                                                     std::int32_t clip, std::int32_t frame,
                                                     const game::battle::EffectBody& place,
                                                     std::int32_t index) const {
  const std::optional<std::optional<std::int32_t>> npc = npcOf(fighter);
  if (!npc || scene_ == nullptr || index < 0 || clip < 0) {
    return std::nullopt;
  }
  const assets::ModelData* model = scene_->fighterModel(*npc);
  if (model == nullptr) {
    return std::nullopt;
  }
  const std::optional<glm::mat4> joint = clipJointAt(*model, static_cast<std::uint32_t>(clip),
                                                     frame, static_cast<std::uint32_t>(index));
  if (!joint) {
    return std::nullopt;
  }
  // The place in the world's axes: the console's y and z the other way, and rotation.vy as a yaw.
  const glm::vec3 at{static_cast<float>(place.location.x), static_cast<float>(-place.location.y),
                     static_cast<float>(-place.location.z)};
  const glm::mat4 world =
      glm::rotate(glm::translate(glm::mat4{1.0F}, at), assets::yawFromOriginal(place.rotationY),
                  glm::vec3{0.0F, 1.0F, 0.0F});
  return originalMatrix(world * *joint);
}

std::optional<game::battle::FighterClips>
BattleEffectBodies::clips(game::battle::FighterId fighter) const {
  const std::optional<std::optional<std::int32_t>> npc = npcOf(fighter);
  if (!npc || scene_ == nullptr) {
    return std::nullopt;
  }
  const assets::ModelData* model = scene_->fighterModel(*npc);
  if (model == nullptr) {
    return std::nullopt;
  }
  return battleClipsOf(*model);
}

std::vector<game::battle::FighterId> BattleEffectBodies::fighters() const {
  std::vector<game::battle::FighterId> out;
  for (game::battle::FighterId id = 0; id < battle_->state().fighters.size(); ++id) {
    out.push_back(id);
  }
  return out;
}

} // namespace dwe::app
