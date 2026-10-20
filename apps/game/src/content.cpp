#include "content.hpp"

#include <dwe/assets/pack_manifest.hpp>
#include <dwe/core/log.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace dwe::app {

core::Result<std::unique_ptr<GameContent>> GameContent::mount(const std::filesystem::path& content,
                                                              std::span<const std::filesystem::path> mods) {
    std::unique_ptr<GameContent> mounted{new GameContent};
    std::vector<std::filesystem::path> folders{content};
    std::error_code error;
    if (std::filesystem::is_directory(content / "packs", error)) {
        folders.push_back(content / "packs");
    }
    std::vector<assets::FoundPack> found;
    const auto add = [&](const std::filesystem::path& folder) -> core::Result<void> {
        auto packs = assets::findPacks(mounted->disk_, folder);
        if (!packs) {
            return std::unexpected{packs.error()};
        }
        found.insert(found.end(), packs->packs.begin(), packs->packs.end());
        return {};
    };
    for (const std::filesystem::path& folder : folders) {
        if (auto added = add(folder); !added) {
            return std::unexpected{added.error()};
        }
    }
    // The game pack lives in the source tree (content/packs/game); a content folder made by extraction alone has
    // only the base pack.
    const bool hasGamePack =
        std::ranges::any_of(found, [](const assets::FoundPack& pack) { return pack.path.stem() == "game"; });
    if (!hasGamePack) {
        if (auto added = add(DWE_TEAM_PACKS); !added) {
            return std::unexpected{added.error()};
        }
    }
    for (const std::filesystem::path& folder : mods) {
        if (auto added = add(folder); !added) {
            return std::unexpected{added.error()};
        }
    }
    if (found.empty()) {
        return core::fail(std::format("no packs in {}", assets::pathText(content)));
    }
    auto opened = assets::openPacks(mounted->disk_, found);
    if (!opened) {
        return std::unexpected{opened.error()};
    }
    mounted->packs_ = std::move(*opened);
    auto files = assets::VirtualFileSystem::mount(mounted->packs_.locations);
    if (!files) {
        return std::unexpected{files.error()};
    }
    mounted->files_ = std::make_unique<assets::VirtualFileSystem>(std::move(*files));
    for (const assets::MountedPack& pack : mounted->files_->packs()) {
        core::logInfo("content: pack {} {} ({}) at {}", pack.manifest.id, assets::formatVersion(pack.manifest.version),
                      assets::layerName(pack.manifest.layer), assets::pathText(pack.root));
    }
    for (const assets::AssetId& id : mounted->files_->overriddenIds()) {
        const assets::AssetSource* source = mounted->files_->resolve(id);
        core::logInfo("content: {} comes from pack {}", id.str(), source != nullptr ? source->packId : "?");
    }
    return mounted;
}

} // namespace dwe::app
