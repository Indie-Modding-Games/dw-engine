#include "script_assets.hpp"

#include <dwe/assets/file_system.hpp>
#include <dwe/core/message.hpp>
#include <dwe/script/engine_code.hpp>

#include <algorithm>
#include <format>

#include "messages.hpp"

namespace dwe::script::detail {
namespace {

constexpr std::string_view LuaExtension = ".lua";

// The largest script the VM compiles. The game's longest scripts are a few tens of KiB; compiling one runs outside any
// call's budget, so a pack cannot make the engine copy and parse a huge text.
constexpr std::size_t MaxScriptBytes = std::size_t{1} << 20;

} // namespace

core::Result<ScriptAsset> readScriptAsset(const assets::VirtualFileSystem& files, const assets::AssetId& id) {
    const assets::AssetSource* source = files.resolve(id);
    if (source == nullptr) {
        return core::fail(core::formatMessage(messages::NoScriptAsset, id.str()));
    }
    if (source->file.extension() != LuaExtension) {
        return core::fail(core::formatMessage(messages::NotLuaScript, id.str(), assets::pathText(source->file)));
    }
    // The file systems have no size query: the cap applies once the bytes are read, before any copy or compile.
    auto bytes = files.read(id);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    if (bytes->size() > MaxScriptBytes) {
        return core::fail(core::formatMessage(messages::ScriptTooLarge, id.str(), bytes->size(), MaxScriptBytes));
    }
    std::string text(bytes->size(), '\0');
    std::ranges::transform(*bytes, text.begin(), [](std::byte b) { return static_cast<char>(b); });
    const auto pack = std::ranges::find(files.packs(), source->packId,
                                        [](const assets::MountedPack& mounted) { return mounted.manifest.id; });
    // A pack the stack does not list is no engine pack.
    const assets::Layer layer = pack != files.packs().end() ? pack->manifest.layer : assets::Layer::User;
    const assets::PackOrigin origin = pack != files.packs().end() ? pack->origin : assets::PackOrigin::Mod;
    return ScriptAsset{.text = std::move(text), .pack = source->packId, .layer = layer, .origin = origin};
}

core::Result<ScriptAsset> readEngineScriptAsset(const assets::VirtualFileSystem& files, const assets::AssetId& id) {
    auto script = readScriptAsset(files, id);
    if (script && !isEngineCode(script->origin, script->layer)) {
        return core::fail(core::formatMessage(messages::EngineScriptFromMod, id.str(), script->pack,
                                              assets::originName(script->origin), assets::layerName(script->layer)));
    }
    return script;
}

std::string assetChunkName(std::string_view pack, const assets::AssetId& id) {
    return std::format("@{}:{}", pack, id.str());
}

} // namespace dwe::script::detail
