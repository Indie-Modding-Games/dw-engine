#pragma once

#include <dwe/game/story/script_motion.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/scene/world.hpp>

#include <optional>

namespace dwe::app {

class FieldScene;

// The story's stage on the field: the characters and the camera its scripted moves drive
// (game::story::ScriptMotion), and the hold a script keeps on the player (script_interp.c:
// scriptPauseGame, tickScript). The player and the partner are on every map; the map's NPCs are
// the field's (game::field::FieldNpcs), by script id.
class StoryStage final : public game::story::ScriptActorsPort,
                         public game::story::FieldControlPort {
public:
  // The scene must outlive the stage.
  explicit StoryStage(FieldScene& scene) : scene_{&scene} {}

  void freeze() override;
  void release() override;

  [[nodiscard]] std::optional<std::int64_t> slotOf(std::int64_t character) const override;
  [[nodiscard]] glm::vec3 position(std::int64_t character) const override;
  [[nodiscard]] float yaw(std::int64_t character) const override;
  void turn(std::int64_t character, float yaw) override;
  void place(std::int64_t character, glm::vec3 position) override;
  void drive(std::int64_t character, glm::vec3 velocity) override;
  void animate(std::int64_t character, std::int64_t animation) override;
  [[nodiscard]] float groundSpeed(std::int64_t character, std::int64_t animation) const override;
  [[nodiscard]] bool wouldTouchCharacter(std::int64_t character, float step) const override;
  void panCamera(glm::vec3 focus, float seconds) override;
  [[nodiscard]] bool cameraPanning() const override;
  void shiftCamera(glm::vec3 by) override;
  void resetOrigin(std::int64_t character) override;
  [[nodiscard]] bool turnMapObject(std::int64_t object, std::int64_t direction) override;

private:
  // The entity of a character on the map.
  [[nodiscard]] std::optional<scene::Entity> entityOf(std::int64_t character) const;

  FieldScene* scene_;
};

} // namespace dwe::app
