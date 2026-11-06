#include <dwe/core/log.hpp>
#include <dwe/session/story_props.hpp>

#include <cstdint>
#include <memory>

#include "story_scenes_context.hpp"

namespace dwe::session {

namespace {

// map_object.c: spawnBoulder places MAP_3D_OBJ[0] at (-300, -500, 2000), unturned, and
// moveBoulder turns it 10 (of 4096 a turn) back about its x axis each move.
constexpr BoulderShown BoulderStart{.shown = true, .x = -300, .y = -500, .z = 2000, .pitch = 0};
constexpr std::int32_t BoulderPitchStep = -10;

class ScenePropOrders final : public StoryPropOrders {
public:
  explicit ScenePropOrders(StoryScenesContext& scenes) : scenes_{&scenes} {}

  void spawnBoulder() override {
    scenes_->boulder = BoulderStart;
    core::logDebug("[port:story] spawn_boulder (map_object.c:616 spawnBoulder)");
    scenes_->propRequests.emplace_back(scenes_->boulder);
  }

  void moveBoulder(std::int64_t down, std::int64_t forward) override {
    // moveBoulder takes int16_t operands and moves the boulder even when it is not shown: only
    // its object is removed, not its place.
    BoulderShown& boulder = scenes_->boulder;
    boulder.y += static_cast<std::int16_t>(down);
    boulder.z += static_cast<std::int16_t>(forward);
    boulder.pitch += BoulderPitchStep;
    scenes_->propRequests.emplace_back(boulder);
  }

  void despawnBoulder() override {
    scenes_->boulder.shown = false;
    core::logDebug("[port:story] despawn_boulder (script_interp.c:2138, removeObject 0xFB6)");
    scenes_->propRequests.emplace_back(scenes_->boulder);
  }

  void spriteAtCharacter(std::int64_t character, std::int64_t node, std::int64_t object) override {
    // The script gives the node and the sprite as bytes (pollNextTwoScriptBytes).
    scenes_->propRequests.emplace_back(
        SpriteAtCharacter{.character = character,
                          .node = static_cast<std::int8_t>(static_cast<std::uint8_t>(node)),
                          .object = static_cast<std::uint8_t>(object)});
  }

private:
  StoryScenesContext* scenes_;
};

} // namespace

std::unique_ptr<StoryPropOrders> makePropOrders(StoryScenesContext& scenes) {
  return std::make_unique<ScenePropOrders>(scenes);
}

} // namespace dwe::session
