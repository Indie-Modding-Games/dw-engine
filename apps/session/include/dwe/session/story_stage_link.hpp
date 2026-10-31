#pragma once

#include <dwe/game/story/script_motion.hpp>

#include <cstdint>
#include <optional>

namespace dwe::session {

// The story's scripted moves reach the field through the stage the app attaches
// (StoryAdapter::attachStage). Without one, no character is on the map, so every move order logs
// and does nothing.
class StoryActorsLink final : public game::story::ScriptActorsPort {
public:
  StoryActorsLink() = default;

  // The stage must outlive the link or be detached first; nothing detaches it.
  void setTarget(game::story::ScriptActorsPort* target) { target_ = target; }

  [[nodiscard]] std::optional<std::int64_t> slotOf(std::int64_t character) const override;
  [[nodiscard]] glm::vec3 position(std::int64_t character) const override;
  [[nodiscard]] float yaw(std::int64_t character) const override;
  void turn(std::int64_t character, float yaw) override;
  void place(std::int64_t character, glm::vec3 position) override;
  void drive(std::int64_t character, glm::vec3 velocity, game::story::Glide glide) override;
  void animate(std::int64_t character, std::int64_t animation) override;
  [[nodiscard]] float groundSpeed(std::int64_t character, std::int64_t animation) const override;
  [[nodiscard]] bool wouldTouchCharacter(std::int64_t character, float step) const override;
  void panCamera(glm::vec3 focus, float seconds) override;
  [[nodiscard]] bool cameraPanning() const override;
  void shiftCamera(glm::vec3 by) override;
  void resetOrigin(std::int64_t character) override;
  [[nodiscard]] bool turnMapObject(std::int64_t object, std::int64_t direction) override;
  [[nodiscard]] std::optional<glm::ivec2> mapSpritePosition(std::int64_t object) const override;
  void placeMapSprite(std::int64_t object, glm::ivec2 position) override;

private:
  game::story::ScriptActorsPort* target_ = nullptr;
};

} // namespace dwe::session
