#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/script/script_coroutine.hpp>

#include <cstddef>
#include <string_view>

namespace dwe::script {

namespace detail {
struct VmState;
}

// The sandboxed environment of one pack's scripts. It has its own copies of the safe standard
// libraries, the API's namespaces as read-only tables, `hooks` and a print that logs the pack's id;
// it cannot see the engine's globals or another pack's. Its globals are fixed: reading or assigning
// any other is an error, so scripts keep their state in locals. Get one from
// ScriptVm::modEnvironment(); it must not outlive the VM.
class ModEnvironment {
public:
  ModEnvironment(detail::VmState& vm, std::size_t index) : vm_{&vm}, index_{index} {}

  [[nodiscard]] std::string_view pack() const;

  // Runs Lua source in the environment; chunkName identifies it in error messages.
  core::Result<void> run(std::string_view source, std::string_view chunkName);

  // Runs a script asset of this pack (`scripts/main` for scripts/main.lua) and remembers it, so
  // ScriptVm::reloadChanged() runs the pack's scripts again when one changes. The file system must
  // outlive the VM.
  core::Result<void> runAsset(const assets::VirtualFileSystem& files, const assets::AssetId& id);

  // Not remembered for reloadChanged(): whoever steps the module's coroutines decides when to load
  // it again.
  core::Result<ScriptModule> loadModule(const assets::VirtualFileSystem& files,
                                        const assets::AssetId& id);
  // A module from source that no file holds (a tool sent it); `name` names it in errors and is
  // its chunk's name.
  core::Result<ScriptModule> loadModuleSource(std::string_view source, std::string_view name);

private:
  detail::VmState* vm_;
  std::size_t index_;
};

} // namespace dwe::script
