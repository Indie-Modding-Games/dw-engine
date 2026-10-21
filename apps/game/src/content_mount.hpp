#pragma once

#include <dwe/content/game_content.hpp>
#include <dwe/core/error.hpp>

#include <memory>

#include "options.hpp"

namespace dwe::app {

// Mounts the game's content: the packs of --content and --mods and, with --dev-assets, the dev assets named by the
// config of the current folder or, failing that, of the user's data folder, with its disc image read through dwx
// (content::openDevAssets, docs/architecture/assets-de-desarrollo.md).
core::Result<std::unique_ptr<content::GameContent>> mountGameContent(const Options& options);

} // namespace dwe::app
