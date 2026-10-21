#include <dwe/core/message.hpp>
#include <dwe/script/engine_code.hpp>

#include <algorithm>
#include <string_view>

#include "messages.hpp"

namespace dwe::script {
namespace {

// Whether the pack whose file wins an id provides engine code; the error names the pack, its origin and its layer.
core::Result<void> checkEngineCode(const assets::VirtualFileSystem& files, const assets::AssetId& id,
                                   std::string_view message) {
    const assets::AssetSource* source = files.resolve(id);
    if (source == nullptr) {
        return core::fail(core::formatMessage(messages::NoCodeAsset, id.str()));
    }
    const auto pack = std::ranges::find(files.packs(), source->packId,
                                        [](const assets::MountedPack& mounted) { return mounted.manifest.id; });
    // A pack the stack does not list is no engine pack.
    const assets::PackOrigin origin = pack != files.packs().end() ? pack->origin : assets::PackOrigin::Mod;
    const assets::Layer layer = pack != files.packs().end() ? pack->manifest.layer : assets::Layer::User;
    if (!isEngineCode(origin, layer)) {
        return core::fail(core::formatMessage(message, id.str(), source->packId, assets::originName(origin),
                                              assets::layerName(layer)));
    }
    return {};
}

} // namespace

bool isEngineCode(assets::PackOrigin origin, assets::Layer layer) {
    return origin == assets::PackOrigin::Engine && assets::isEngineLayer(layer);
}

core::Result<void> checkEngineScript(const assets::VirtualFileSystem& files, const assets::AssetId& id) {
    return checkEngineCode(files, id, messages::EngineScriptFromMod);
}

core::Result<void> checkEngineDocument(const assets::VirtualFileSystem& files, const assets::AssetId& id) {
    return checkEngineCode(files, id, messages::EngineDocumentFromMod);
}

} // namespace dwe::script
