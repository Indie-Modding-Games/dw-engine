#include <dwe/core/log.hpp>
#include <dwe/script/api_registry.hpp>
#include <dwe/script/script_vm.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/story_lua.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace dwe::session {

namespace {

using script::ApiCall;
using script::ApiValue;

} // namespace

// call_routine, start_battle and call_partner_routine (script_interp.c: scriptInstruction64to7E).
// Each order waits: on the task it starts, or on a done task when it goes on in the same frame.
core::Result<void> StoryLuaHost::publishSceneApi(StorySceneOrders orders) {
  if (orders.routines == nullptr || orders.battles == nullptr || orders.partner == nullptr) {
    return core::fail(std::string{messages::NoSceneOrders});
  }
  script::ApiRegistry api = vm_->api();
  std::vector<core::Result<void>> published;
  published.push_back(api.publishWaiting(
      StoryNamespace, story_api::CallRoutine,
      [this, routines = orders.routines](const ApiCall& call) -> core::Result<ApiValue> {
        const auto routine = call.integer(0);
        if (!routine) {
          return std::unexpected{routine.error()};
        }
        auto start = routines->call(*routine);
        if (!start) {
          return std::unexpected{start.error()};
        }
        if (std::holds_alternative<game::story::RoutineYields>(*start)) {
          pendingWait_ = game::story::FrameWait{.frames = 1, .result = {}};
        } else if (const auto* waits = std::get_if<game::story::RoutineWaits>(&*start)) {
          pendingTask_ = waits->task;
        } else {
          pendingTask_ = std::make_shared<game::story::DoneTask>();
        }
        return ApiValue{};
      }));
  published.push_back(api.publishWaiting(
      StoryNamespace, story_api::StartBattle,
      [this, battles = orders.battles](const ApiCall& /*call*/) -> core::Result<ApiValue> {
        auto task = battles->startBattle();
        if (!task) {
          return std::unexpected{task.error()};
        }
        pendingTask_ = std::move(*task);
        return ApiValue{};
      }));
  published.push_back(
      api.publishWaiting(StoryNamespace, story_api::CallPartnerRoutine,
                         [this, partner = orders.partner, freeze = std::move(orders.freeze)](
                             const ApiCall& call) -> core::Result<ApiValue> {
                           const auto routine = call.integer(0);
                           if (!routine) {
                             return std::unexpected{routine.error()};
                           }
                           // script_interp.c: the order freezes the world, starts the scene and
                           // goes on; the script waits for the scene itself (wait_for_entity 255).
                           if (freeze) {
                             freeze();
                           }
                           if (auto called = partner->call(*routine); !called) {
                             return std::unexpected{called.error()};
                           }
                           pendingTask_ = std::make_shared<game::story::DoneTask>();
                           return ApiValue{};
                         }));
  const auto failed = std::ranges::find_if(published, [](const auto& result) { return !result; });
  return failed == published.end() ? core::Result<void>{} : *failed;
}

// The props' orders (script_interp.c: scriptInstruction64to7E, SCRIPT_OP_SPAWN_BOULDER,
// SCRIPT_OP_MOVE_BOULDER, SCRIPT_OP_DESPAWN_BOULDER, SCRIPT_OP_SPAWN_SPRITE_AT_ENTITY). The
// boulder's orders read a first byte they never use. move_boulder waits a frame (its longjmp 2), so
// each move of the boulder shows; the others go on in the same frame.
core::Result<void> StoryLuaHost::publishPropApi(StoryPropOrders& props) {
  script::ApiRegistry api = vm_->api();
  std::vector<core::Result<void>> published;
  published.push_back(api.publish(StoryNamespace, story_api::SpawnBoulder,
                                  [&props](const ApiCall& /*call*/) -> core::Result<ApiValue> {
                                    props.spawnBoulder();
                                    return ApiValue{};
                                  }));
  published.push_back(
      api.publishWaiting(StoryNamespace, story_api::MoveBoulder,
                         [this, &props](const ApiCall& call) -> core::Result<ApiValue> {
                           const auto down = call.integer(1);
                           if (!down) {
                             return std::unexpected{down.error()};
                           }
                           const auto forward = call.integer(2);
                           if (!forward) {
                             return std::unexpected{forward.error()};
                           }
                           props.moveBoulder(*down, *forward);
                           pendingWait_ = game::story::FrameWait{.frames = 1, .result = {}};
                           return ApiValue{};
                         }));
  published.push_back(api.publish(StoryNamespace, story_api::DespawnBoulder,
                                  [&props](const ApiCall& /*call*/) -> core::Result<ApiValue> {
                                    props.despawnBoulder();
                                    return ApiValue{};
                                  }));
  published.push_back(api.publish(StoryNamespace, story_api::SpawnSpriteAtEntity,
                                  [&props](const ApiCall& call) -> core::Result<ApiValue> {
                                    std::array<std::int64_t, 3> operands{};
                                    for (std::size_t index = 0; index < operands.size(); ++index) {
                                      const auto operand = call.integer(index);
                                      if (!operand) {
                                        return std::unexpected{operand.error()};
                                      }
                                      operands.at(index) = *operand;
                                    }
                                    props.spriteAtCharacter(operands[0], operands[1], operands[2]);
                                    return ApiValue{};
                                  }));
  const auto failed = std::ranges::find_if(published, [](const auto& result) { return !result; });
  return failed == published.end() ? core::Result<void>{} : *failed;
}

} // namespace dwe::session
