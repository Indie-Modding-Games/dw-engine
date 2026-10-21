#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/pack_manifest.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>

#include <string>
#include <string_view>

namespace dwe::script::detail {

// A Lua script read from the packs: its text and the pack whose file won.
struct ScriptAsset {
    std::string text;
    std::string pack;
    assets::Layer layer = assets::Layer::Mods;
    assets::PackOrigin origin = assets::PackOrigin::Mod;
};

// Reads the Lua script that provides an asset id. A file of another type is an error, so data never runs as code.
core::Result<ScriptAsset> readScriptAsset(const assets::VirtualFileSystem& files, const assets::AssetId& id);

// Reads a script asset that runs with the engine's globals, which only the engine's own layers (base, remaster) may
// provide: a mod that replaced it would get the engine's privileges, such as the developer console's commands.
core::Result<ScriptAsset> readEngineScriptAsset(const assets::VirtualFileSystem& files, const assets::AssetId& id);

// The chunk name of a script asset, so its errors read `pack:id:line: ...`.
std::string assetChunkName(std::string_view pack, const assets::AssetId& id);

} // namespace dwe::script::detail
