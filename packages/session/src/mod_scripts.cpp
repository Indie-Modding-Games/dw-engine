#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/log.hpp>
#include <dwe/script/script_vm.hpp>
#include <dwe/session/game_hooks.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/mod_scripts.hpp>
#include <dwe/session/story_adapter.hpp>

#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::session {

core::Result<void> ModScripts::startNewGame(GameSession& session) {
  bind(session);
  return {};
}

void ModScripts::afterLoad(GameSession& session) {
  bind(session);
}

void ModScripts::tick(GameSession& session, const ruleset::ClockAdvance& /*advance*/) {
  bind(session);
}

void ModScripts::bind(GameSession& session) {
  if (bound_) {
    return;
  }
  bound_ = true;
  script::ScriptVm* vm = story_->vm();
  if (vm == nullptr) {
    core::logWarning("{}", messages::ModScriptsNoVm);
    return;
  }
  if (auto bound = session.hooks().bind(vm->hooks()); !bound) {
    core::logError("{}", bound.error().message);
    return;
  }
  runEntryScripts();
}

void ModScripts::runEntryScripts() {
  const assets::VirtualFileSystem& files = story_->files();
  const auto id = assets::AssetId::parse(ModEntryScript);
  if (!id) {
    return;
  }
  // From the bottom of the stack up, as the story's patches load.
  for (const assets::AssetSource& source : files.sources(*id)) {
    const assets::MountedPack& pack = files.packOf(source);
    const std::string name = std::format("{}:{}", pack.manifest.id, ModEntryScript);
    auto environment = story_->vm()->modEnvironment(pack.manifest);
    auto bytes = environment
                     ? files.read(source)
                     : core::Result<std::vector<std::byte>>{std::unexpected{environment.error()}};
    if (!bytes) {
      core::logError("{}", messages::formatMessage(messages::ModScriptFailed, pack.manifest.id,
                                                   ModEntryScript, bytes.error().message));
      continue;
    }
    const std::string_view text{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
    if (auto ran = environment->run(text, "=" + name); !ran) {
      core::logError("{}", messages::formatMessage(messages::ModScriptFailed, pack.manifest.id,
                                                   ModEntryScript, ran.error().message));
      continue;
    }
    ran_.push_back(pack.manifest.id);
    core::logInfo(
        "{}", messages::formatMessage(messages::ModScriptRan, pack.manifest.id, ModEntryScript));
  }
}

} // namespace dwe::session
