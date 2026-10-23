#include <dwe/script/api_registry.hpp>
#include <dwe/script/script_vm.hpp>
#include <dwe/session/story_lua.hpp>
#include <dwe/session/story_npcs.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace dwe::session {

namespace {

using game::story::NpcOrders;
using script::ApiCall;
using script::ApiValue;

using OneOperandNpcOrder = core::Result<void> (NpcOrders::*)(std::int64_t);
using TwoOperandNpcOrder = core::Result<void> (NpcOrders::*)(std::int64_t, std::int64_t);

struct NamedNpcOrder1 {
  std::string_view name;
  OneOperandNpcOrder run;
};

struct NamedNpcOrder2 {
  std::string_view name;
  TwoOperandNpcOrder run;
};

constexpr std::array OneOperandNpcOrders{
    NamedNpcOrder1{.name = story_api::LoadSpeciesModel, .run = &NpcOrders::loadSpeciesModel},
    NamedNpcOrder1{.name = story_api::UnloadEntity, .run = &NpcOrders::unloadEntity},
    NamedNpcOrder1{.name = story_api::UnloadSpeciesModel, .run = &NpcOrders::unloadSpeciesModel},
};

constexpr std::array TwoOperandNpcOrders{
    NamedNpcOrder2{.name = story_api::SetAutotalk, .run = &NpcOrders::setAutotalk},
};

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

core::Result<void> StoryLuaHost::publishNpcApi(NpcOrders& orders) {
  script::ApiRegistry api = vm_->api();
  std::vector<core::Result<void>> published;
  for (const NamedNpcOrder1& order : OneOperandNpcOrders) {
    published.push_back(
        api.publish(StoryNamespace, order.name,
                    [&orders, run = order.run](const ApiCall& call) -> core::Result<ApiValue> {
                      const auto operands = integers<1>(call);
                      if (!operands) {
                        return std::unexpected{operands.error()};
                      }
                      return nothingFrom((orders.*run)((*operands)[0]));
                    }));
  }
  for (const NamedNpcOrder2& order : TwoOperandNpcOrders) {
    published.push_back(
        api.publish(StoryNamespace, order.name,
                    [&orders, run = order.run](const ApiCall& call) -> core::Result<ApiValue> {
                      const auto operands = integers<2>(call);
                      if (!operands) {
                        return std::unexpected{operands.error()};
                      }
                      return nothingFrom((orders.*run)((*operands)[0], (*operands)[1]));
                    }));
  }
  published.push_back(api.publish(StoryNamespace, story_api::SpawnNpc,
                                  [&orders](const ApiCall& call) -> core::Result<ApiValue> {
                                    const auto operands = integers<3>(call);
                                    if (!operands) {
                                      return std::unexpected{operands.error()};
                                    }
                                    return nothingFrom(orders.spawnNpc(
                                        (*operands)[0], (*operands)[1], (*operands)[2]));
                                  }));
  const auto failed = std::ranges::find_if(published, [](const auto& result) { return !result; });
  return failed == published.end() ? core::Result<void>{} : *failed;
}

} // namespace dwe::session
