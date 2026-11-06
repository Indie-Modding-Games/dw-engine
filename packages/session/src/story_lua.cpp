#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/story_modules.hpp>
#include <dwe/script/api_registry.hpp>
#include <dwe/script/script_vm.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/story_lua.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace dwe::session {
namespace {

using game::story::FileJump;
using game::story::SectionEnded;
using game::story::SectionFailed;
using game::story::SectionRef;
using game::story::SectionReturned;
using game::story::SectionStep;
using game::story::StoryOrders;
using script::ApiCall;
using script::ApiValue;

using OneOperandOrder = core::Result<void> (StoryOrders::*)(std::int64_t);
using TwoOperandOrder = core::Result<void> (StoryOrders::*)(std::int64_t, std::int64_t);

struct NamedOrder1 {
  std::string_view name;
  OneOperandOrder run;
};

struct NamedOrder2 {
  std::string_view name;
  TwoOperandOrder run;
};

constexpr std::array OneOperandOrders{
    NamedOrder1{.name = story_api::SetTrigger, .run = &StoryOrders::setTrigger},
    NamedOrder1{.name = story_api::UnsetTrigger, .run = &StoryOrders::unsetTrigger},
};

constexpr std::array TwoOperandOrders{
    NamedOrder2{.name = story_api::SetVariable, .run = &StoryOrders::setVariable},
    NamedOrder2{.name = story_api::AddToVariable, .run = &StoryOrders::addToVariable},
    NamedOrder2{.name = story_api::ReduceVariable, .run = &StoryOrders::reduceVariable},
    NamedOrder2{.name = story_api::CopyVariable, .run = &StoryOrders::copyVariable},
    NamedOrder2{.name = story_api::StoreRandom, .run = &StoryOrders::storeRandom},
};

core::Result<ApiValue> nothing(const core::Result<void>& done) {
  if (!done) {
    return std::unexpected{done.error()};
  }
  return ApiValue{};
}

assets::AssetId moduleId(std::uint16_t script) {
  const auto id = assets::AssetId::parse(std::format("story/{:03}", script));
  DWE_ASSERT(id.has_value());
  return *id;
}

core::Result<SectionRef> sectionRef(std::int64_t script, std::int64_t section) {
  constexpr std::int64_t Highest = std::numeric_limits<std::uint16_t>::max();
  if (script < 0 || script > Highest || section < 0 || section > Highest) {
    return core::fail(messages::formatMessage(messages::NoStorySection, script, section));
  }
  return SectionRef{.script = static_cast<std::uint16_t>(script),
                    .section = static_cast<std::uint16_t>(section)};
}

} // namespace

StoryLuaHost::StoryLuaHost(script::ScriptVm& vm, const assets::VirtualFileSystem& files)
    : vm_{&vm}, files_{&files} {}

core::Result<void> StoryLuaHost::publishApi(game::story::StoryStatePort& state,
                                            game::story::StoryOrders& orders,
                                            game::story::SoundPort& sound) {
  script::ApiRegistry api = vm_->api();
  std::vector<core::Result<void>> published;
  published.push_back(api.publishValue(StoryNamespace, story_api::Ended, ApiValue{true}));
  published.push_back(api.publish(StoryNamespace, story_api::Trigger,
                                  [&state](const ApiCall& call) -> core::Result<ApiValue> {
                                    const auto trigger = call.integer(0);
                                    if (!trigger) {
                                      return std::unexpected{trigger.error()};
                                    }
                                    const auto set = state.trigger(*trigger);
                                    if (!set) {
                                      return std::unexpected{set.error()};
                                    }
                                    return ApiValue{*set};
                                  }));
  published.push_back(api.publish(StoryNamespace, story_api::Variable,
                                  [&state](const ApiCall& call) -> core::Result<ApiValue> {
                                    const auto variable = call.integer(0);
                                    if (!variable) {
                                      return std::unexpected{variable.error()};
                                    }
                                    const auto value = state.variable(*variable);
                                    if (!value) {
                                      return std::unexpected{value.error()};
                                    }
                                    return ApiValue{*value};
                                  }));
  for (const NamedOrder1& order : OneOperandOrders) {
    published.push_back(
        api.publish(StoryNamespace, order.name,
                    [&orders, run = order.run](const ApiCall& call) -> core::Result<ApiValue> {
                      const auto operand = call.integer(0);
                      if (!operand) {
                        return std::unexpected{operand.error()};
                      }
                      return nothing((orders.*run)(*operand));
                    }));
  }
  for (const NamedOrder2& order : TwoOperandOrders) {
    published.push_back(
        api.publish(StoryNamespace, order.name,
                    [&orders, run = order.run](const ApiCall& call) -> core::Result<ApiValue> {
                      const auto first = call.integer(0);
                      if (!first) {
                        return std::unexpected{first.error()};
                      }
                      const auto second = call.integer(1);
                      if (!second) {
                        return std::unexpected{second.error()};
                      }
                      return nothing((orders.*run)(*first, *second));
                    }));
  }
  published.push_back(api.publishWaiting(StoryNamespace, story_api::Delay,
                                         [this](const ApiCall& call) -> core::Result<ApiValue> {
                                           const auto frames = call.integer(0);
                                           if (!frames) {
                                             return std::unexpected{frames.error()};
                                           }
                                           pendingWait_ = StoryOrders::delay(*frames);
                                           return ApiValue{};
                                         }));
  published.push_back(api.publishWaiting(
      StoryNamespace, story_api::JumpToFile, [this](const ApiCall& call) -> core::Result<ApiValue> {
        const auto script = call.integer(0);
        if (!script) {
          return std::unexpected{script.error()};
        }
        const auto section = call.integer(1);
        if (!section) {
          return std::unexpected{section.error()};
        }
        const auto target = sectionRef(*script, *section);
        if (!target) {
          return std::unexpected{target.error()};
        }
        pendingWait_ = FileJump{.target = *target, .linkDepth = 0};
        return ApiValue{};
      }));
  // story.play_sound(slot, sound), op 0x5A: as script_interp.c: scriptInstruction5Ato5F, it hands
  // both operands to the sound slots (sound.c: playSound) and goes on; a sound that cannot play
  // stays silent.
  published.push_back(api.publish(StoryNamespace, story_api::PlaySound,
                                  [&sound](const ApiCall& call) -> core::Result<ApiValue> {
                                    const auto slot = call.integer(0);
                                    if (!slot) {
                                      return std::unexpected{slot.error()};
                                    }
                                    const auto value = call.integer(1);
                                    if (!value) {
                                      return std::unexpected{value.error()};
                                    }
                                    core::logDebug("[port:audio] scriptInstruction5Ato5F "
                                                   "(script_interp.c:1550): play_sound({}, {})",
                                                   *slot, *value);
                                    sound.playSound(*slot, *value);
                                    return ApiValue{};
                                  }));
  const auto failed = std::ranges::find_if(published, [](const auto& result) { return !result; });
  return failed == published.end() ? core::Result<void>{} : *failed;
}

core::Result<void> StoryLuaHost::checkModules() const {
  const assets::AssetId first = moduleId(0);
  if (files_->resolve(first) == nullptr) {
    core::logInfo("{}", messages::NoStoryModules);
    return {};
  }
  return checkHeader(first);
}

core::Result<void> StoryLuaHost::checkHeader(const assets::AssetId& id) const {
  const auto bytes = files_->read(id);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  const std::string_view text{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
  const std::string header =
      std::format("{}{}:", core::story_modules::HeaderPrefix, core::story_modules::Version);
  if (!text.starts_with(header)) {
    return core::fail(messages::formatMessage(messages::StoryModuleOutdated, id.str(),
                                              core::story_modules::HeaderPrefix,
                                              core::story_modules::Version));
  }
  return {};
}

core::Result<const script::ScriptModule*> StoryLuaHost::module(std::uint16_t script) {
  if (const auto loaded = modules_.find(script); loaded != modules_.end()) {
    return &loaded->second;
  }
  const assets::AssetId id = moduleId(script);
  const assets::AssetSource* source = files_->resolve(id);
  const auto pack = source == nullptr ? files_->packs().end()
                                      : std::ranges::find(files_->packs(), source->packId,
                                                          [](const assets::MountedPack& mounted) {
                                                            return mounted.manifest.id;
                                                          });
  if (pack == files_->packs().end()) {
    return core::fail(messages::formatMessage(messages::NoStoryModule, id.str()));
  }
  if (auto checked = checkHeader(id); !checked) {
    return std::unexpected{checked.error()};
  }
  auto environment = vm_->modEnvironment(pack->manifest);
  if (!environment) {
    return std::unexpected{environment.error()};
  }
  auto loaded = environment->loadModule(*files_, id);
  if (!loaded) {
    return std::unexpected{loaded.error()};
  }
  return &modules_.emplace(script, std::move(*loaded)).first->second;
}

core::Result<game::story::SectionHandle> StoryLuaHost::start(SectionRef section) {
  const auto loaded = module(section.script);
  if (!loaded) {
    return std::unexpected{loaded.error()};
  }
  const std::array<ApiValue, 2> path{ApiValue{std::string{"sections"}},
                                     ApiValue{static_cast<std::int64_t>(section.section)}};
  auto coroutine = (*loaded)->coroutine(path);
  if (!coroutine) {
    return std::unexpected{coroutine.error()};
  }
  const game::story::SectionHandle handle = nextHandle_++;
  sections_.emplace(handle, std::move(*coroutine));
  return handle;
}

std::optional<SectionStep> StoryLuaHost::driveTask(game::story::SectionHandle handle,
                                                   game::story::ResumeValue& value) {
  const auto waiting = tasks_.find(handle);
  if (waiting == tasks_.end()) {
    return std::nullopt;
  }
  const game::story::TaskStep step = waiting->second.task->tick();
  if (std::holds_alternative<game::story::TaskRunning>(step)) {
    return game::story::Wait{game::story::FrameWait{.frames = 1, .result = {}}};
  }
  const std::uint32_t linkDepth = waiting->second.linkDepth;
  tasks_.erase(waiting);
  if (const auto* done = std::get_if<game::story::TaskDone>(&step)) {
    value = done->value;
    return std::nullopt;
  }
  if (const auto* jump = std::get_if<game::story::TaskJump>(&step)) {
    return game::story::Wait{FileJump{.target = jump->target, .linkDepth = linkDepth}};
  }
  return game::story::Wait{std::get<game::story::TaskMapChange>(step).change};
}

SectionStep StoryLuaHost::resume(game::story::SectionHandle handle,
                                 game::story::ResumeValue value) {
  const auto found = sections_.find(handle);
  DWE_ASSERT(found != sections_.end());
  // A task that ends in the frame it starts lets the section go on and wait again; a section that
  // only ever does that would never give the frame back.
  constexpr int MostTasksInAFrame = 256;
  for (int round = 0; round < MostTasksInAFrame; ++round) {
    if (auto step = driveTask(handle, value)) {
      return *step;
    }
    pendingWait_.reset();
    pendingTask_.reset();
    const ApiValue resumed = value ? ApiValue{*value} : ApiValue{};
    const script::CoroutineStep step = found->second.resume(resumed);
    if (const auto* waiting = std::get_if<script::CoroutineWaiting>(&step)) {
      if (pendingTask_) {
        tasks_.insert_or_assign(handle, WaitingTask{.task = std::exchange(pendingTask_, nullptr),
                                                    .linkDepth = waiting->callDepth});
        value.reset();
        continue;
      }
      if (!pendingWait_) {
        return SectionFailed{.message = std::string{messages::WaitWithoutStoryWait}};
      }
      game::story::Wait wait = *std::exchange(pendingWait_, std::nullopt);
      if (auto* jump = std::get_if<FileJump>(&wait)) {
        jump->linkDepth = waiting->callDepth;
      }
      return wait;
    }
    if (const auto* finished = std::get_if<script::CoroutineFinished>(&step)) {
      if (finished->value == ApiValue{true}) {
        return SectionEnded{};
      }
      if (std::holds_alternative<std::monostate>(finished->value)) {
        return SectionReturned{};
      }
      return SectionFailed{.message = std::string{messages::BadSectionReturn}};
    }
    return SectionFailed{.message = std::get<script::CoroutineFailed>(step).message};
  }
  return SectionFailed{.message = std::string{messages::TooManyStoryTasks}};
}

void StoryLuaHost::close(game::story::SectionHandle handle) {
  tasks_.erase(handle);
  sections_.erase(handle);
}

} // namespace dwe::session
