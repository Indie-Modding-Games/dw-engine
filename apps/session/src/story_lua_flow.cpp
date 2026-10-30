#include <dwe/core/log.hpp>
#include <dwe/game/story/story_runner.hpp>
#include <dwe/script/api_registry.hpp>
#include <dwe/script/script_vm.hpp>
#include <dwe/session/story_flow.hpp>
#include <dwe/session/story_lua.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::session {

namespace {

using game::story::StoryOrders;
using game::story::StoryRunner;
using script::ApiCall;
using script::ApiValue;

constexpr std::string_view NoRunner = "the story runner is not built yet";

core::Result<ApiValue> nothingFrom(const core::Result<void>& done) {
  if (!done) {
    return std::unexpected{done.error()};
  }
  return ApiValue{};
}

// The integer operands of a call, or the first that is not one.
template <std::size_t Count>
core::Result<std::array<std::int64_t, Count>> integers(const ApiCall& call) {
  std::array<std::int64_t, Count> values{};
  for (std::size_t index = 0; index < Count; ++index) {
    const auto value = call.integer(index);
    if (!value) {
      return std::unexpected{value.error()};
    }
    values.at(index) = *value;
  }
  return values;
}

} // namespace

void StoryMapChanges::startMapChange(std::int32_t map, std::int32_t exit, bool showName) {
  core::logDebug("[port:story] scriptTickChangeMap (map.c:5277): map {} exit {} name {}", map, exit,
                 showName);
  if (!target_) {
    core::logError("[port:story] the map change to map {} exit {} is not played: the player stays",
                   map, exit);
    unplayed_ = true;
    return;
  }
  if (!target_(map, exit, showName)) {
    // The target says why: no such map is an error, a map the story lock keeps closed is not.
    core::logDebug("[port:story] the map change to map {} exit {} is not played: the player stays",
                   map, exit);
    unplayed_ = true;
  }
}

void StoryFieldControl::setTarget(StoryHoldTarget target) {
  target_ = std::move(target);
  apply();
}

void StoryFieldControl::freeze() {
  held_ = true;
  apply();
}

void StoryFieldControl::release() {
  held_ = false;
  apply();
}

void StoryFieldControl::apply() {
  core::logDebug("[port:story] scriptPauseGame (script_interp.c:2727): player held {}", held_);
  if (target_) {
    target_(held_);
  }
}

core::Result<void> StoryLuaHost::publishFlowApi(StoryOrders& orders,
                                                const std::function<StoryRunner*()>& runner) {
  script::ApiRegistry api = vm_->api();
  std::vector<core::Result<void>> published;
  // script_interp.c: scriptInstructionFBtoFF (set_script) and scriptInstruction64to7E
  // (section_on_exit) change the runner's script flow.
  published.push_back(api.publish(StoryNamespace, story_api::SetScript,
                                  [runner](const ApiCall& call) -> core::Result<ApiValue> {
                                    const auto operands = integers<2>(call);
                                    if (!operands) {
                                      return std::unexpected{operands.error()};
                                    }
                                    StoryRunner* flow = runner();
                                    if (flow == nullptr) {
                                      return core::fail(std::string{NoRunner});
                                    }
                                    return nothingFrom(
                                        flow->setScript((*operands)[0], (*operands)[1]));
                                  }));
  published.push_back(api.publish(StoryNamespace, story_api::SectionOnExit,
                                  [runner](const ApiCall& call) -> core::Result<ApiValue> {
                                    const auto section = call.integer(0);
                                    if (!section) {
                                      return std::unexpected{section.error()};
                                    }
                                    StoryRunner* flow = runner();
                                    if (flow == nullptr) {
                                      return core::fail(std::string{NoRunner});
                                    }
                                    return nothingFrom(flow->sectionOnExit(*section));
                                  }));
  // script_interp.c: scriptInstruction46to58: warp_to and teleport end the section with a map
  // change (MapChangeWait).
  published.push_back(api.publishWaiting(
      StoryNamespace, story_api::WarpTo, [this](const ApiCall& call) -> core::Result<ApiValue> {
        const auto operands = integers<3>(call);
        if (!operands) {
          return std::unexpected{operands.error()};
        }
        auto change = StoryOrders::warpTo((*operands)[0], (*operands)[1], (*operands)[2]);
        if (!change) {
          return std::unexpected{change.error()};
        }
        pendingWait_ = *change;
        return ApiValue{};
      }));
  published.push_back(
      api.publishWaiting(StoryNamespace, story_api::Teleport,
                         [this, &orders](const ApiCall& call) -> core::Result<ApiValue> {
                           const auto variable = call.integer(0);
                           if (!variable) {
                             return std::unexpected{variable.error()};
                           }
                           auto change = orders.teleport(*variable);
                           if (!change) {
                             return std::unexpected{change.error()};
                           }
                           pendingWait_ = *change;
                           return ApiValue{};
                         }));
  const auto failed = std::ranges::find_if(published, [](const auto& result) { return !result; });
  return failed == published.end() ? core::Result<void>{} : *failed;
}

} // namespace dwe::session
