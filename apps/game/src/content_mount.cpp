#include "content_mount.hpp"

#include <dwe/assets/dev_asset_sources.hpp>
#include <dwe/content/dev_assets.hpp>
#include <dwe/content/user_data.hpp>
#include <dwe/core/log.hpp>
#include <dwe/platform/platform.hpp>

#include <filesystem>
#include <utility>

namespace dwe::app {

core::Result<std::unique_ptr<content::GameContent>> mountGameContent(const Options& options,
                                                                     bool withUiAddons) {
  assets::DevAssetSources devAssets;
  if (options.devAssets) {
    auto opened = content::openDevAssets();
    if (!opened) {
      return std::unexpected{opened.error()};
    }
    devAssets = std::move(*opened);
  }
  std::filesystem::path userData;
  if (withUiAddons) {
    if (auto folder =
            platform::userDataFolder(content::UserDataOrganization, content::UserDataApplication)) {
      userData = std::move(*folder);
    } else {
      core::logWarning("game: no UI addons from the user data folder: {}", folder.error().message);
    }
  }
  return content::GameContent::mount(options.content, options.mods, devAssets, userData);
}

} // namespace dwe::app
