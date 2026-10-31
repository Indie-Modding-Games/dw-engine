#include <dwe/game/story/script_motion.hpp>

#include <doctest/doctest.h>

#include <map>
#include <numbers>

namespace {

using dwe::game::story::Glide;
using dwe::game::story::MotionRules;
using dwe::game::story::PlayerScriptId;
using dwe::game::story::ScriptActorsPort;
using dwe::game::story::ScriptMotion;

constexpr std::int64_t Vegiemon = 6;
constexpr float FrameSeconds = 1.0F / 30.0F;

// The player and one NPC on an open floor; drive moves a character by its velocity each frame.
class Floor final : public ScriptActorsPort {
public:
  struct Actor {
    std::int64_t slot = 0;
    glm::vec3 position{0.0F};
    float yaw = 0.0F;
    glm::vec3 velocity{0.0F};
    std::int64_t animation = -1;
  };
  std::map<std::int64_t, Actor> actors;

  void step() {
    for (auto& [id, actor] : actors) {
      actor.position += actor.velocity * FrameSeconds;
    }
  }

  [[nodiscard]] std::optional<std::int64_t> slotOf(std::int64_t character) const override {
    const auto found = actors.find(character);
    return found == actors.end() ? std::nullopt : std::optional{found->second.slot};
  }
  [[nodiscard]] glm::vec3 position(std::int64_t character) const override {
    return actors.at(character).position;
  }
  [[nodiscard]] float yaw(std::int64_t character) const override {
    return actors.at(character).yaw;
  }
  void turn(std::int64_t character, float yaw) override { actors.at(character).yaw = yaw; }
  void place(std::int64_t character, glm::vec3 position) override {
    actors.at(character).position = position;
  }
  void drive(std::int64_t character, glm::vec3 velocity, Glide /*glide*/) override {
    actors.at(character).velocity = velocity;
  }
  void animate(std::int64_t character, std::int64_t animation) override {
    actors.at(character).animation = animation;
  }
  [[nodiscard]] float groundSpeed(std::int64_t /*character*/,
                                  std::int64_t /*animation*/) const override {
    return 900.0F;
  }
  [[nodiscard]] bool wouldTouchCharacter(std::int64_t /*character*/,
                                         float /*step*/) const override {
    return false;
  }
  void panCamera(glm::vec3 /*focus*/, float /*seconds*/) override {}
  [[nodiscard]] bool cameraPanning() const override { return false; }
  void shiftCamera(glm::vec3 /*by*/) override {}
  void resetOrigin(std::int64_t /*character*/) override {}
  [[nodiscard]] bool turnMapObject(std::int64_t /*object*/, std::int64_t /*direction*/) override {
    return true;
  }
  [[nodiscard]] std::optional<glm::ivec2>
  mapSpritePosition(std::int64_t /*object*/) const override {
    return std::nullopt;
  }
  void placeMapSprite(std::int64_t /*object*/, glm::ivec2 /*position*/) override {}
};

} // namespace

TEST_CASE("a turn that takes a walking character's slot stops the walk when it ends") {
  // trop03's Vegiemon (script 15, section 0x33): the player walks off toward (-1024, -1820) and,
  // ten frames later, entity_look_at_entity takes slot 0 to face Vegiemon. tickScriptedMovement
  // starts the player's resting animation when the turn is done, which carries nobody: the
  // player stops there instead of running on into the jungle.
  Floor floor;
  floor.actors[PlayerScriptId] = {.slot = 0, .position = {-708.0F, 0.0F, 2323.0F}};
  floor.actors[Vegiemon] = {.slot = 3, .position = {911.0F, 0.0F, 2266.0F}};
  ScriptMotion motion{floor, MotionRules{.frameSeconds = FrameSeconds,
                                         .lookTurnPerFrame = std::numbers::pi_v<float> / 8.0F}};
  motion.walkToWithCamera(PlayerScriptId, -1024, -1820, 1);
  for (int frame = 0; frame < 10; ++frame) {
    motion.scriptFrame();
    floor.step();
  }
  CHECK(glm::length(floor.actors[PlayerScriptId].velocity) > 0.0F);
  motion.lookAt(PlayerScriptId, Vegiemon);
  const auto done = motion.waitFor(PlayerScriptId);
  for (int frame = 0; frame < 30 && !done(); ++frame) {
    motion.scriptFrame();
    floor.step();
  }
  REQUIRE(done());
  CHECK(floor.actors[PlayerScriptId].velocity == glm::vec3{0.0F});
  CHECK(floor.actors[PlayerScriptId].animation == 0);
  const glm::vec3 stopped = floor.actors[PlayerScriptId].position;
  for (int frame = 0; frame < 30; ++frame) {
    motion.scriptFrame();
    floor.step();
  }
  CHECK(floor.actors[PlayerScriptId].position == stopped);
}
