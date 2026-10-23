#include <dwe/script/api_registry.hpp>
#include <dwe/script/script_vm.hpp>
#include <dwe/session/story_lua.hpp>
#include <dwe/session/story_motion_api.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

// The scripted moves' orders (script_interp.c: scriptInstruction46to58, scriptInstruction64to7E).
// Every one but play_animation and wait_for_entity freezes the world first (scriptPauseGame).
namespace dwe::session {
namespace {

using game::story::ScriptMotion;
using script::ApiCall;
using script::ApiValue;

constexpr std::size_t MaxOperands = 5;
using Operands = std::array<std::int64_t, MaxOperands>;

struct MotionOrder {
  std::string_view name;
  std::size_t operands = 0;
  bool freezes = true;
  std::function<void(const Operands&)> run;
};

core::Result<Operands> readOperands(const ApiCall& call, std::size_t count) {
  Operands operands{};
  for (std::size_t index = 0; index < count; ++index) {
    const auto operand = call.integer(index);
    if (!operand) {
      return std::unexpected{operand.error()};
    }
    operands.at(index) = *operand;
  }
  return operands;
}

std::vector<MotionOrder> motionOrders(ScriptMotion& motion) {
  namespace api = story_motion_api;
  // The operands as the emitter writes them; the walks' and entity_move_to's last operand is
  // never read by the original.
  return {
      {.name = api::LookAt,
       .operands = 2,
       .freezes = true,
       .run = [&motion](const Operands& o) { motion.lookAt(o[0], o[1]); }},
      {.name = api::SetRotation,
       .operands = 2,
       .freezes = true,
       .run = [&motion](const Operands& o) { motion.setRotation(o[0], o[1]); }},
      {.name = api::WalkTo,
       .operands = 4,
       .freezes = true,
       .run = [&motion](const Operands& o) { motion.walkTo(o[0], o[1], o[2], o[3]); }},
      {.name = api::WalkToEntity,
       .operands = 3,
       .freezes = true,
       .run = [&motion](const Operands& o) { motion.walkToCharacter(o[0], o[1], o[2]); }},
      {.name = api::MoveTo,
       .operands = 4,
       .freezes = true,
       .run = [&motion](const Operands& o) { motion.moveTo(o[0], o[1], o[2], o[3]); }},
      {.name = api::MoveToAxis,
       .operands = 4,
       .freezes = true,
       .run = [&motion](const Operands& o) { motion.moveToAxis(o[0], o[1], o[2], o[3]); }},
      {.name = api::CameraTo,
       .operands = 3,
       .freezes = true,
       .run = [&motion](const Operands& o) { motion.cameraTo(o[0], o[1], o[2]); }},
      {.name = api::CameraToEntity,
       .operands = 2,
       .freezes = true,
       .run = [&motion](const Operands& o) { motion.cameraToCharacter(o[0], o[1]); }},
      {.name = api::PlayAnimation,
       .operands = 2,
       .freezes = false,
       .run = [&motion](const Operands& o) { motion.playAnimation(o[0], o[1]); }},
  };
}

} // namespace

core::Result<void> StoryLuaHost::publishMotionApi(game::story::StoryRunner& runner,
                                                  ScriptMotion& motion) {
  namespace api = story_motion_api;
  script::ApiRegistry registry = vm_->api();
  std::vector<core::Result<void>> published;
  published.push_back(
      registry.publishValue(StoryNamespace, api::Player, ApiValue{game::story::PlayerScriptId}));
  published.push_back(
      registry.publishValue(StoryNamespace, api::Partner, ApiValue{game::story::PartnerScriptId}));
  for (MotionOrder& order : motionOrders(motion)) {
    published.push_back(registry.publish(
        StoryNamespace, order.name,
        [&runner, order = std::move(order)](const ApiCall& call) -> core::Result<ApiValue> {
          const auto operands = readOperands(call, order.operands);
          if (!operands) {
            return std::unexpected{operands.error()};
          }
          if (order.freezes) {
            runner.freezeWorld();
          }
          order.run(*operands);
          return ApiValue{};
        }));
  }
  published.push_back(registry.publishWaiting(
      StoryNamespace, api::WaitForEntity,
      [this, &motion](const ApiCall& call) -> core::Result<ApiValue> {
        const auto what = call.integer(0);
        if (!what) {
          return std::unexpected{what.error()};
        }
        pendingWait_ = game::story::UntilWait{.done = motion.waitFor(*what)};
        return ApiValue{};
      }));
  const auto failed = std::ranges::find_if(published, [](const auto& result) { return !result; });
  return failed == published.end() ? core::Result<void>{} : *failed;
}

} // namespace dwe::session
