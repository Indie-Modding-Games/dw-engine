#include "content_mount.hpp"

#include <dwe/assets/dev_asset_sources.hpp>
#include <dwe/content/dev_assets.hpp>

#include <utility>

namespace dwe::app {

core::Result<std::unique_ptr<content::GameContent>> mountGameContent(const Options& options) {
    assets::DevAssetSources devAssets;
    if (options.devAssets) {
        auto opened = content::openDevAssets();
        if (!opened) {
            return std::unexpected{opened.error()};
        }
        devAssets = std::move(*opened);
    }
    return content::GameContent::mount(options.content, options.mods, devAssets);
}

} // namespace dwe::app
