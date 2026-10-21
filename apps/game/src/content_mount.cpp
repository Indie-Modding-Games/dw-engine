#include "content_mount.hpp"

#include <dwe/assets/dev_asset_sources.hpp>
#include <dwe/content/dev_assets.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/platform/platform.hpp>

#include <filesystem>
#include <optional>
#include <utility>

#include "common.hpp"
#include "messages.hpp"

namespace dwe::app {
namespace {

// The user's data folder, where the game also looks for the dev assets config; logged when the system has none.
std::optional<std::filesystem::path> userDataFolder() {
    auto folder = platform::userDataFolder(UserDataOrganization, UserDataApplication);
    if (!folder) {
        core::logWarning("{}", core::formatMessage(messages::NoDevAssetsDataFolder, folder.error().message));
        return std::nullopt;
    }
    return std::move(*folder);
}

} // namespace

core::Result<std::unique_ptr<content::GameContent>> mountGameContent(const Options& options) {
    assets::DevAssetSources devAssets;
    if (options.devAssets) {
        auto loaded = content::loadDevAssets(content::devAssetsConfigFolders(userDataFolder()));
        if (!loaded) {
            return std::unexpected{loaded.error()};
        }
        devAssets = std::move(*loaded);
    }
    return content::GameContent::mount(options.content, options.mods, devAssets);
}

} // namespace dwe::app
