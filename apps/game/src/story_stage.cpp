#include "story_stage.hpp"

#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/field/field.hpp>

#include <array>
#include <cstdint>
#include <utility>

#include "field_scene.hpp"

namespace dwe::app {

namespace {

using game::story::PartnerScriptId;
using game::story::PlayerScriptId;

// The move slots of the player and the partner (script_value.c: scriptIdToEntityId).
constexpr std::int64_t PlayerSlot = 0;
constexpr std::int64_t PartnerSlot = 1;

float groundDistance(glm::vec3 from, glm::vec3 to) {
  return glm::length(glm::vec2{to.x - from.x, to.z - from.z});
}

} // namespace

void StoryStage::freeze() {
  scene_->field().holdForScript(true);
}

void StoryStage::release() {
  scene_->field().holdForScript(false);
}

std::optional<std::int64_t> StoryStage::slotOf(std::int64_t character) const {
  if (character == PlayerScriptId) {
    return PlayerSlot;
  }
  if (character == PartnerScriptId) {
    return PartnerSlot;
  }
  return std::nullopt;
}

std::optional<scene::Entity> StoryStage::entityOf(std::int64_t character) const {
  const game::field::Field& field = scene_->field();
  if (character == PlayerScriptId) {
    return field.playerEntity();
  }
  if (character == PartnerScriptId) {
    return field.partnerEntity();
  }
  return std::nullopt;
}

glm::vec3 StoryStage::position(std::int64_t character) const {
  const game::field::Field& field = scene_->field();
  return character == PartnerScriptId ? field.partner().position : field.player().position;
}

float StoryStage::yaw(std::int64_t character) const {
  const game::field::Field& field = scene_->field();
  return character == PartnerScriptId ? field.partner().yaw : field.player().yaw;
}

void StoryStage::turn(std::int64_t character, float yaw) {
  if (const auto entity = entityOf(character)) {
    scene_->field().turnByScript(*entity, yaw);
  }
}

void StoryStage::place(std::int64_t character, glm::vec3 position) {
  if (const auto entity = entityOf(character)) {
    scene_->field().placeByScript(*entity, position);
  }
}

void StoryStage::drive(std::int64_t character, glm::vec3 velocity) {
  if (const auto entity = entityOf(character)) {
    scene_->field().driveByScript(*entity, velocity);
  }
}

void StoryStage::animate(std::int64_t character, std::int64_t animation) {
  if (const auto entity = entityOf(character); entity && animation >= 0) {
    scene_->field().animateByScript(*entity, static_cast<std::uint32_t>(animation));
  }
}

float StoryStage::groundSpeed(std::int64_t character, std::int64_t animation) const {
  const auto entity = entityOf(character);
  if (!entity || animation < 0) {
    return 0.0F;
  }
  return scene_->clipGroundSpeed(*entity, static_cast<std::uint32_t>(animation));
}

bool StoryStage::wouldTouchCharacter(std::int64_t character, float step) const {
  // map.c: entityCheckCollision steps the character forward and tests the others' circles.
  const game::field::Field& field = scene_->field();
  const game::field::FieldRules& rules = field.rules();
  const glm::vec3 ahead = position(character) + assets::forwardOfYaw(yaw(character)) * step;
  const float radius =
      character == PartnerScriptId ? rules.partner.mover.radius : rules.player.radius;
  const std::array others{
      std::pair{PlayerScriptId, rules.player.radius},
      std::pair{PartnerScriptId, rules.partner.mover.radius},
  };
  for (const auto& [other, otherRadius] : others) {
    if (other != character && groundDistance(ahead, position(other)) < radius + otherRadius) {
      return true;
    }
  }
  return false;
}

void StoryStage::panCamera(glm::vec3 focus, float seconds) {
  scene_->field().panCameraByScript(focus, seconds);
}

bool StoryStage::cameraPanning() const {
  return scene_->field().cameraPanning();
}

} // namespace dwe::app
