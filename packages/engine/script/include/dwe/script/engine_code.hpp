#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/pack_manifest.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>

namespace dwe::script {

// Code that runs as the engine, with its globals and privileges (the console's commands, every API
// namespace, no pack quota), comes only from a pack the engine found in its own folders
// (PackOrigin::Engine) that declares one of the engine's layers, base or remaster. Where a pack was
// found decides, never its manifest alone, which the pack writes itself. A mod's code runs in its
// own environment. This is the one owner of that rule; the engine's script assets and the UI's
// documents both ask it.
[[nodiscard]] bool isEngineCode(assets::PackOrigin origin, assets::Layer layer);

// Whether the file that wins an asset id may hold a script that runs as the engine, such as a
// document's <script src>. The error names the pack and the layer that provide it.
core::Result<void> checkEngineScript(const assets::VirtualFileSystem& files,
                                     const assets::AssetId& id);

// Whether a document may run the Lua written inside it, its inline <script> and event attributes
// such as onclick, as the engine. A document from any other layer still loads; only its own code
// does not run.
core::Result<void> checkEngineDocument(const assets::VirtualFileSystem& files,
                                       const assets::AssetId& id);

} // namespace dwe::script
