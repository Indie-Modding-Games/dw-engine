#include "story_stage.hpp"

#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/game/field/npcs.hpp>
#include <dwe/scene/components.hpp>

#include <algorithm>
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
  // An NPC's slot is its instance, 2 + its record's slot (script_value.c: scriptIdToEntityId).
  if (!std::in_range<std::int32_t>(character)) {
    return std::nullopt;
  }
  const auto instance = scene_->field().npcs().instanceOf(static_cast<std::int32_t>(character));
  if (!instance) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(*instance);
}

std::optional<scene::Entity> StoryStage::entityOf(std::int64_t character) const {
  const game::field::Field& field = scene_->field();
  if (character == PlayerScriptId) {
    return field.playerEntity();
  }
  if (character == PartnerScriptId) {
    return field.partnerEntity();
  }
  if (!std::in_range<std::int32_t>(character)) {
    return std::nullopt;
  }
  return field.npcs().entityOf(static_cast<std::int32_t>(character));
}

glm::vec3 StoryStage::position(std::int64_t character) const {
  const game::field::Field& field = scene_->field();
  if (character != PlayerScriptId && character != PartnerScriptId) {
    if (const auto entity = entityOf(character)) {
      return scene_->world().registry().get<scene::Transform>(*entity).position;
    }
  }
  return character == PartnerScriptId ? field.partner().position : field.player().position;
}

float StoryStage::yaw(std::int64_t character) const {
  const game::field::Field& field = scene_->field();
  if (character != PlayerScriptId && character != PartnerScriptId) {
    if (const auto entity = entityOf(character)) {
      return scene_->world().registry().get<scene::Transform>(*entity).yaw;
    }
  }
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
  // overworld.c: setActiveAnim: an NPC's script animation keeps its own tick off it.
  if (character != PlayerScriptId && character != PartnerScriptId) {
    if (std::in_range<std::int32_t>(character) && std::in_range<std::int32_t>(animation)) {
      scene_->field().npcs().setScriptedAnimation(static_cast<std::int32_t>(character),
                                                  static_cast<std::int32_t>(animation));
    }
    return;
  }
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
  return std::ranges::any_of(others, [&](const auto& entry) {
    const auto& [other, otherRadius] = entry;
    return other != character && groundDistance(ahead, position(other)) < radius + otherRadius;
  });
}

void StoryStage::panCamera(glm::vec3 focus, float seconds) {
  scene_->field().panCameraByScript(focus, seconds);
}

bool StoryStage::cameraPanning() const {
  return scene_->field().cameraPanning();
}

} // namespace dwe::app
