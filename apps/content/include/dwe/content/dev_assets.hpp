#pragma once

#include <dwe/assets/dev_asset_sources.hpp>
#include <dwe/core/error.hpp>

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace dwe::content {

// The command line option of the game and dwe-sim that turns the dev assets on. Off by default, in every build.
inline constexpr std::string_view DevAssetsOption = "--dev-assets";
// The file that names the dev assets' roots. It holds paths of one machine, so it is never versioned.
inline constexpr std::string_view DevAssetsConfigName = "dev-assets.yaml";
// The text key of the banner shown, and logged, while the dev assets are in use.
inline constexpr std::string_view DevAssetsBannerKey = "dev.assets_active";

// Reads the text of a dev assets config: disc_image, extracted_content (a list) and decomp_root, all optional but
// not all missing. A relative path is relative to `folder`, the config's folder; every path is made lexically normal.
core::Result<assets::DevAssetSources> parseDevAssetsConfig(std::string_view yaml, const std::filesystem::path& folder);

// Where the dev assets config is looked for, in order: the current folder, so each worktree may have its own, then the
// user's data folder when the app has one. A current folder that cannot be known is logged and left out.
[[nodiscard]] std::vector<std::filesystem::path>
devAssetsConfigFolders(const std::optional<std::filesystem::path>& userData);

// Reads the config of the first of these folders that has one, checks its roots and logs each of them, so the dev
// assets are never in use in silence. Fails when no folder has a config.
core::Result<assets::DevAssetSources> loadDevAssets(std::span<const std::filesystem::path> configFolders);

// The dev assets of the game and dwe-sim: the config of the current folder or of the user's data folder, with the disc
// image it names read through dwx (dev_disc.hpp). A user data folder that cannot be found is logged and left out.
core::Result<assets::DevAssetSources> openDevAssets();

} // namespace dwe::content
