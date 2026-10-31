#include <dwe/core/log.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/story_stage_link.hpp>

#include <cstdint>
#include <optional>

namespace dwe::session {

std::optional<std::int64_t> StoryActorsLink::slotOf(std::int64_t character) const {
  if (target_ == nullptr) {
    core::logDebug("[port:story] scriptIdToEntityId (script_value.c): no stage for character {}",
                   character);
    return std::nullopt;
  }
  return target_->slotOf(character);
}

glm::vec3 StoryActorsLink::position(std::int64_t character) const {
  return target_ != nullptr ? target_->position(character) : glm::vec3{};
}

float StoryActorsLink::yaw(std::int64_t character) const {
  return target_ != nullptr ? target_->yaw(character) : 0.0F;
}

void StoryActorsLink::turn(std::int64_t character, float yaw) {
  if (target_ != nullptr) {
    target_->turn(character, yaw);
  }
}

void StoryActorsLink::place(std::int64_t character, glm::vec3 position) {
  if (target_ != nullptr) {
    target_->place(character, position);
  }
}

void StoryActorsLink::drive(std::int64_t character, glm::vec3 velocity, game::story::Glide glide) {
  if (target_ != nullptr) {
    target_->drive(character, velocity, glide);
  }
}

void StoryActorsLink::animate(std::int64_t character, std::int64_t animation) {
  if (target_ != nullptr) {
    target_->animate(character, animation);
  }
}

float StoryActorsLink::groundSpeed(std::int64_t character, std::int64_t animation) const {
  return target_ != nullptr ? target_->groundSpeed(character, animation) : 0.0F;
}

bool StoryActorsLink::wouldTouchCharacter(std::int64_t character, float step) const {
  return target_ != nullptr && target_->wouldTouchCharacter(character, step);
}

void StoryActorsLink::panCamera(glm::vec3 focus, float seconds) {
  if (target_ != nullptr) {
    target_->panCamera(focus, seconds);
  }
}

bool StoryActorsLink::cameraPanning() const {
  return target_ != nullptr && target_->cameraPanning();
}

void StoryActorsLink::shiftCamera(glm::vec3 by) {
  if (target_ != nullptr) {
    target_->shiftCamera(by);
  }
}

void StoryActorsLink::resetOrigin(std::int64_t character) {
  if (target_ != nullptr) {
    target_->resetOrigin(character);
  }
}

// Without a stage there is no object to turn: the turn is over at once.
bool StoryActorsLink::turnMapObject(std::int64_t object, std::int64_t direction) {
  return target_ == nullptr || target_->turnMapObject(object, direction);
}

std::optional<glm::ivec2> StoryActorsLink::mapSpritePosition(std::int64_t object) const {
  return target_ != nullptr ? target_->mapSpritePosition(object) : std::nullopt;
}

void StoryActorsLink::placeMapSprite(std::int64_t object, glm::ivec2 position) {
  if (target_ != nullptr) {
    target_->placeMapSprite(object, position);
  }
}

// The scripted moves' rules come with the stage, from the field rules: until then the moves have
// none, which no order reaches since no character is on the map.
void StoryAdapter::attachStage(game::story::ScriptActorsPort* stage,
                               const game::story::MotionRules& rules) {
  if (actors_ == nullptr || !motion_.has_value()) {
    return;
  }
  actors_->setTarget(stage);
  if (stage != nullptr) {
    // In place, so the runner and the move orders keep pointing at it; a stage comes with a game's
    // field, before any script of it runs.
    motion_.emplace(*actors_, rules);
  }
}

void StoryAdapter::setPartnerSync(bool raised) {
  if (!motion_.has_value()) {
    return;
  }
  motion_->connectPartnerSync();
  motion_->setPartnerSync(raised);
}

core::Result<void> StoryAdapter::publishMotion() {
  if (!runner_ || !motion_) {
    return {};
  }
  if (auto published = host_->publishMotionApi(*runner_, *motion_); !published) {
    return published;
  }
  return {};
}

} // namespace dwe::session
