#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/field/field.hpp>

#include <algorithm>

#include "script_messages.hpp"

namespace dwe::game::field {

using scene::AnimationIntent;
using scene::Transform;

void Field::holdForScript(bool held) {
  if (held == scriptHeld_) {
    return;
  }
  scriptHeld_ = held;
  entt::registry& registry = world_->registry();
  core::logDebug("{}", core::formatMessage(script_messages::Held, held));
  if (held) {
    // script_interp.c: scriptPauseGame stops following the player: the camera stays put.
    scriptCamera_ = registry.get<Transform>(player_).position;
    moveTarget_.reset();
    path_.clear();
    registry.emplace_or_replace<ScriptedActor>(player_);
    registry.emplace_or_replace<ScriptedActor>(partner_);
  } else {
    // script_interp.c: tickScript gives control, the camera and the clock back.
    scriptCamera_.reset();
    cameraPan_.reset();
    registry.remove<ScriptedActor>(player_);
    registry.remove<ScriptedActor>(partner_);
  }
  registry.get<AnimationIntent>(player_) = {};
  registry.get<AnimationIntent>(partner_) = {};
}

void Field::driveByScript(scene::Entity entity, glm::vec3 velocity) {
  world_->registry().get_or_emplace<ScriptedActor>(entity).velocity = velocity;
}

void Field::placeByScript(scene::Entity entity, glm::vec3 position) {
  world_->registry().get<Transform>(entity).position = position;
}

void Field::turnByScript(scene::Entity entity, float yaw) {
  world_->registry().get<Transform>(entity).yaw = yaw;
}

void Field::animateByScript(scene::Entity entity, std::uint32_t clip) {
  core::logDebug(
      "{}", core::formatMessage(script_messages::Clip, static_cast<std::uint32_t>(entity), clip));
  world_->registry().get_or_emplace<ScriptedActor>(entity).clip = clip;
}

void Field::panCameraByScript(glm::vec3 focus, float seconds) {
  const glm::vec3 from =
      scriptCamera_.value_or(world_->registry().get<Transform>(player_).position);
  core::logDebug("{}", core::formatMessage(script_messages::Pan, focus.x, focus.z, seconds));
  scriptCamera_ = from;
  cameraPan_ = CameraPan{.from = from, .to = focus, .seconds = seconds, .elapsed = 0.0F};
}

void Field::shiftCameraByScript(glm::vec3 by) {
  scriptCamera_ = scriptCamera_.value_or(world_->registry().get<Transform>(player_).position) + by;
}

void Field::tickScripted(float seconds) {
  entt::registry& registry = world_->registry();
  registry.get<AnimationIntent>(player_) = {};
  // The characters a script holds glide where it drives them; the player's and the partner's
  // transforms began this tick in Field::tick.
  registry.view<ScriptedActor, Transform>().each(
      [seconds](const ScriptedActor& actor, Transform& transform) {
        transform.position += actor.velocity * seconds;
      });
  if (cameraPan_) {
    cameraPan_->elapsed += seconds;
    const float done = cameraPan_->seconds > 0.0F
                           ? std::min(cameraPan_->elapsed / cameraPan_->seconds, 1.0F)
                           : 1.0F;
    scriptCamera_ = cameraPan_->from + (cameraPan_->to - cameraPan_->from) * done;
    if (done >= 1.0F) {
      cameraPan_.reset();
    }
  }
  // A scripted map change's fade in runs on while the script holds the player (map.c:
  // scriptTickChangeMap).
  if (fadeInSeconds_) {
    *fadeInSeconds_ += seconds;
    if (fadeFrame(*fadeInSeconds_) > rules_.transition.fadeInFrames) {
      fadeInSeconds_.reset();
    }
  }
}

} // namespace dwe::game::field
