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
#include <optional>
#include <string>
#include <string_view>
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

void StoryLuaHost::loadPatches() {
  if (std::exchange(patchesLoaded_, true)) {
    return;
  }
  // Bottom of the stack first, so a later patch of a section wins; within a pack, by id.
  std::vector<std::pair<std::size_t, assets::AssetId>> found;
  for (const assets::AssetId& id : files_->ids()) {
    if (!id.str().starts_with(StoryPatchFolder)) {
      continue;
    }
    const assets::AssetSource* source = files_->resolve(id);
    const auto pack =
        std::ranges::find(files_->packs(), source->packId,
                          [](const assets::MountedPack& mounted) { return mounted.manifest.id; });
    DWE_ASSERT(pack != files_->packs().end());
    found.emplace_back(static_cast<std::size_t>(pack - files_->packs().begin()), id);
  }
  std::ranges::stable_sort(found, {}, &std::pair<std::size_t, assets::AssetId>::first);
  for (const auto& [pack, id] : found) {
    addPatch(id, files_->packs()[pack]);
  }
}

void StoryLuaHost::addPatch(const assets::AssetId& id, const assets::MountedPack& pack) {
  const std::string name = std::format("{}:{}", pack.manifest.id, id.str());
  auto environment = vm_->modEnvironment(pack.manifest);
  if (!environment) {
    core::logError("{}", messages::formatMessage(messages::StoryPatchFailed, name,
                                                 environment.error().message));
    return;
  }
  auto loaded = environment->loadModule(*files_, id);
  if (!loaded) {
    core::logError(
        "{}", messages::formatMessage(messages::StoryPatchFailed, name, loaded.error().message));
    return;
  }
  constexpr std::int64_t Highest = std::numeric_limits<std::uint16_t>::max();
  const std::array<ApiValue, 1> scriptPath{ApiValue{std::string{"script"}}};
  const auto script = loaded->value(scriptPath);
  const auto* number = script ? std::get_if<std::int64_t>(&*script) : nullptr;
  if (number == nullptr || *number < 0 || *number > Highest) {
    core::logError("{}", messages::formatMessage(messages::StoryPatchNoScript, name));
    return;
  }
  const std::array<ApiValue, 1> sectionsPath{ApiValue{std::string{"sections"}}};
  const auto sections = loaded->functionKeys(sectionsPath);
  if (!sections) {
    core::logError(
        "{}", messages::formatMessage(messages::StoryPatchFailed, name, sections.error().message));
    return;
  }
  if (const auto outside = std::ranges::find_if(
          *sections, [](std::int64_t section) { return section < 0 || section > Highest; });
      outside != sections->end()) {
    core::logError("{}", messages::formatMessage(messages::StoryPatchBadSection, name, *outside));
    return;
  }
  if (sections->empty()) {
    core::logWarning("{}", messages::formatMessage(messages::StoryPatchEmpty, name));
    return;
  }
  const auto patchScript = static_cast<std::uint16_t>(*number);
  const std::size_t index = patches_.size();
  patches_.push_back(Patch{.module = std::move(*loaded), .script = patchScript});
  std::string listed;
  for (const std::int64_t section : *sections) {
    const SectionKey key{patchScript, static_cast<std::uint16_t>(section)};
    if (const auto earlier = patchedSections_.find(key); earlier != patchedSections_.end()) {
      core::logWarning("{}",
                       messages::formatMessage(messages::StoryPatchShadowed, patchScript, section,
                                               name, patches_[earlier->second].module.name()));
    }
    patchedSections_.insert_or_assign(key, index);
    listed += std::format("{}0x{:04x}", listed.empty() ? "" : ", ", section);
  }
  core::logInfo("{}",
                messages::formatMessage(messages::StoryPatchLoaded, name, patchScript, listed));
}

std::optional<std::string> StoryLuaHost::patchOf(SectionRef section) {
  loadPatches();
  const auto patched = patchedSections_.find({section.script, section.section});
  if (patched == patchedSections_.end()) {
    return std::nullopt;
  }
  return patches_[patched->second].module.name();
}

core::Result<void> StoryLuaHost::reloadScripts() {
  if (!sections_.empty()) {
    return core::fail("a story section is running");
  }
  core::logInfo("story: dropping {} loaded modules and {} patches; they load again from the packs",
                modules_.size(), patches_.size());
  patchedSections_.clear();
  patches_.clear();
  modules_.clear();
  patchesLoaded_ = false;
  return {};
}

core::Result<game::story::SectionHandle> StoryLuaHost::start(SectionRef section) {
  loadPatches();
  const std::array<ApiValue, 2> path{ApiValue{std::string{"sections"}},
                                     ApiValue{static_cast<std::int64_t>(section.section)}};
  const auto patched = patchedSections_.find({section.script, section.section});
  const bool staged = staged_ && !staged_->handle && staged_->ref.script == section.script &&
                      staged_->ref.section == section.section;
  // The module is optional under a patch: a patch may give a script no pack has.
  const bool needsModule = (patched == patchedSections_.end() && !staged) ||
                           files_->resolve(moduleId(section.script)) != nullptr;
  const script::ScriptModule* original = nullptr;
  if (needsModule) {
    const auto loaded = module(section.script);
    if (!loaded) {
      return std::unexpected{loaded.error()};
    }
    original = *loaded;
  }
  auto coroutine = [&] {
    if (staged) {
      core::logInfo("[story] script {} section 0x{:04x} runs the section a tool sent",
                    section.script, section.section);
      return staged_->module.coroutine(path,
                                       script::ModuleFunction{.module = original, .path = path});
    }
    if (patched == patchedSections_.end()) {
      return original->coroutine(path);
    }
    const script::ScriptModule& patch = patches_[patched->second].module;
    core::logDebug("[mod:story] script {} section 0x{:04x} runs the patch {}", section.script,
                   section.section, patch.name());
    return patch.coroutine(path, script::ModuleFunction{.module = original, .path = path});
  }();
  if (!coroutine) {
    return std::unexpected{coroutine.error()};
  }
  const game::story::SectionHandle handle = nextHandle_++;
  sections_.emplace(handle, std::move(*coroutine));
  if (staged) {
    staged_->handle = handle;
  }
  return handle;
}

std::vector<std::int32_t> StoryLuaHost::lines(game::story::SectionHandle handle) const {
  const auto found = sections_.find(handle);
  return found == sections_.end() ? std::vector<std::int32_t>{} : found->second.lines();
}

core::Result<void> StoryLuaHost::stageSection(SectionRef section, std::string_view source,
                                              std::optional<std::string_view> pack) {
  if (staged_ && staged_->handle) {
    return core::fail("a section sent before still runs");
  }
  const auto& packs = files_->packs();
  if (packs.empty()) {
    return core::fail("no pack is mounted");
  }
  auto owner = packs.begin();
  if (pack) {
    owner = std::ranges::find(packs, *pack, [](const assets::MountedPack& mounted) {
      return std::string_view{mounted.manifest.id};
    });
    if (owner == packs.end()) {
      return core::fail(std::format("no pack '{}' is mounted", *pack));
    }
  } else if (const assets::AssetSource* module = files_->resolve(moduleId(section.script))) {
    const auto giver =
        std::ranges::find(packs, module->packId,
                          [](const assets::MountedPack& mounted) { return mounted.manifest.id; });
    owner = giver != packs.end() ? giver : owner;
  }
  auto environment = vm_->modEnvironment(owner->manifest);
  if (!environment) {
    return std::unexpected{environment.error()};
  }
  const std::string name = std::format("story_run_{:03}_{:04x}", section.script, section.section);
  auto loaded = environment->loadModuleSource(source, name);
  if (!loaded) {
    return std::unexpected{loaded.error()};
  }
  const std::array<ApiValue, 1> scriptPath{ApiValue{std::string{"script"}}};
  const auto script = loaded->value(scriptPath);
  const auto* number = script ? std::get_if<std::int64_t>(&*script) : nullptr;
  if (number == nullptr || *number != section.script) {
    return core::fail(std::format("the source is not a patch of script {}", section.script));
  }
  const std::array<ApiValue, 1> sectionsPath{ApiValue{std::string{"sections"}}};
  const auto sections = loaded->functionKeys(sectionsPath);
  if (!sections || !std::ranges::contains(*sections, std::int64_t{section.section})) {
    return core::fail(std::format("the source has no section 0x{:04x}", section.section));
  }
  staged_.emplace(StagedSection{.ref = section, .module = std::move(*loaded), .handle = {}});
  return {};
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
  if (staged_ && staged_->handle == handle) {
    staged_.reset();
  }
}

} // namespace dwe::session
