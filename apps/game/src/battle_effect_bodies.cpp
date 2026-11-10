#include "battle_effect_bodies.hpp"

#include <dwe/assets/model_data.hpp>

#include <glm/vec3.hpp>

#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
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

game::battle::Point BattleEffectBodies::ground(const EffectVector& at) const {
  // The way back from originalOf: the battle's ground is the world's x and z.
  return {.x = at.x, .z = -at.z};
}

std::optional<EffectMatrix> BattleEffectBodies::node(game::battle::FighterId fighter,
                                                     std::int32_t index) const {
  const std::optional<std::optional<std::int32_t>> npc = npcOf(fighter);
  const std::optional<game::battle::EffectBody> place = body(fighter);
  if (!npc || !place || scene_ == nullptr || index < 0) {
    return std::nullopt;
  }
  const assets::ModelData* model = scene_->fighterModel(*npc);
  const std::optional<std::span<const assets::NodeTransform>> pose = scene_->fighterPose(*npc);
  if (model == nullptr || !pose) {
    return std::nullopt;
  }
  const std::optional<std::vector<game::battle::BonePose>> chain =
      boneChain(*model, *pose, static_cast<std::uint32_t>(scene_->fighterEffectJoint(*npc, index)));
  if (!chain) {
    return std::nullopt;
  }
  return game::battle::boneMatrix(*place, *chain);
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
  const std::optional<std::vector<game::battle::BonePose>> chain =
      clipBoneChain(*model, static_cast<std::uint32_t>(clip), frame,
                    static_cast<std::uint32_t>(scene_->fighterEffectJoint(*npc, index)));
  if (!chain) {
    return std::nullopt;
  }
  return game::battle::boneMatrix(place, *chain);
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
