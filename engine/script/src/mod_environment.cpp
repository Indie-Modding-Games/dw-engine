#include <dwe/core/message.hpp>
#include <dwe/script/mod_environment.hpp>

#include "messages.hpp"
#include "mod_tables.hpp"
#include "script_assets.hpp"
#include "vm_state.hpp"

namespace dwe::script {

std::string_view ModEnvironment::pack() const {
  return vm_->mods[index_].pack;
}

core::Result<void> ModEnvironment::run(std::string_view source, std::string_view chunkName) {
  const detail::ModState& mod = vm_->mods[index_];
  return detail::runInEnvironment(*vm_, mod.tables, mod.memoryOwner, source, chunkName);
}

core::Result<void> ModEnvironment::runAsset(const assets::VirtualFileSystem& files,
                                            const assets::AssetId& id) {
  auto script = detail::readScriptAsset(files, id);
  if (!script) {
    return std::unexpected{script.error()};
  }
  detail::ModState& mod = vm_->mods[index_];
  if (script->pack != mod.pack) {
    return core::fail(
        core::formatMessage(messages::AssetOfAnotherPack, id.str(), script->pack, mod.pack));
  }
  detail::rememberAsset(mod.assets, files, id, script->text);
  return detail::runInEnvironment(*vm_, mod.tables, mod.memoryOwner, script->text,
                                  detail::assetChunkName(mod.pack, id));
}

} // namespace dwe::script
