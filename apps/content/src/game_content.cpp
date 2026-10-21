#include <dwe/assets/dev_pack_selection.hpp>
#include <dwe/assets/pack_manifest.hpp>
#include <dwe/content/game_content.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "messages.hpp"

namespace dwe::content {
namespace {

// The packs of the content folder, its packs/ folder, the team's packs when it has no game pack, and the mods.
core::Result<std::vector<assets::FoundPack>> findNormalPacks(const assets::FileSystem& disk,
                                                             const std::filesystem::path& content,
                                                             std::span<const std::filesystem::path> mods) {
    std::vector<std::filesystem::path> folders{content};
    std::error_code error;
    if (std::filesystem::is_directory(content / "packs", error)) {
        folders.push_back(content / "packs");
    }
    std::vector<assets::FoundPack> found;
    const auto add = [&](const std::filesystem::path& folder) -> core::Result<void> {
        auto packs = assets::findPacks(disk, folder);
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
    return found;
}

void logShadowed(std::span<const assets::ShadowedDevPack> shadowed) {
    for (const assets::ShadowedDevPack& pack : shadowed) {
        core::logWarning("{}", core::formatMessage(messages::ShadowedDevPack, pack.id, assets::pathText(pack.pack),
                                                   assets::pathText(pack.winner)));
    }
}

// The packs to mount: the normal ones, then the dev ones that their ids pick (selectDevPacks), which fill gaps.
core::Result<std::vector<assets::PackLocation>> stackedLocations(std::span<const assets::PackLocation> opened,
                                                                 std::size_t normalCount) {
    const std::span<const assets::PackLocation> normal = opened.first(normalCount);
    auto selection = assets::selectDevPacks(normal, opened.subspan(normalCount));
    if (!selection) {
        return std::unexpected{selection.error()};
    }
    logShadowed(selection->shadowed);
    std::vector<assets::PackLocation> locations{normal.begin(), normal.end()};
    locations.insert(locations.end(), selection->packs.begin(), selection->packs.end());
    return locations;
}

} // namespace

core::Result<std::unique_ptr<GameContent>> GameContent::mount(const std::filesystem::path& content,
                                                              std::span<const std::filesystem::path> mods,
                                                              const assets::DevAssetSources& devAssets) {
    std::unique_ptr<GameContent> mounted{new GameContent};
    auto found = findNormalPacks(mounted->disk_, content, mods);
    if (!found) {
        return std::unexpected{found.error()};
    }
    const std::size_t normalCount = found->size();
    auto dev = assets::findDevPacks(mounted->disk_, devAssets.extractedContent);
    if (!dev) {
        return std::unexpected{dev.error()};
    }
    found->insert(found->end(), dev->begin(), dev->end());
    if (found->empty()) {
        return core::fail(core::formatMessage(messages::NoPacks, assets::pathText(content)));
    }
    auto opened = assets::openPacks(mounted->disk_, *found);
    if (!opened) {
        return std::unexpected{opened.error()};
    }
    mounted->packs_ = std::move(*opened);
    auto locations = stackedLocations(mounted->packs_.locations, normalCount);
    if (!locations) {
        return std::unexpected{locations.error()};
    }
    auto files = assets::VirtualFileSystem::mount(*locations);
    if (!files) {
        return std::unexpected{files.error()};
    }
    mounted->files_ = std::make_unique<assets::VirtualFileSystem>(std::move(*files));
    mounted->logStack();
    return mounted;
}

std::vector<std::filesystem::path> GameContent::devPacks() const {
    std::vector<std::filesystem::path> roots;
    for (const assets::MountedPack& pack : files_->packs()) {
        if (pack.fillsGaps) {
            roots.push_back(pack.root);
        }
    }
    return roots;
}

void GameContent::logStack() const {
    for (const assets::MountedPack& pack : files_->packs()) {
        core::logInfo("{}", core::formatMessage(pack.fillsGaps ? messages::MountedDevPack : messages::MountedPack,
                                                pack.manifest.id, assets::formatVersion(pack.manifest.version),
                                                assets::layerName(pack.manifest.layer), assets::pathText(pack.root)));
    }
    for (const assets::AssetId& id : files_->overriddenIds()) {
        const assets::AssetSource* source = files_->resolve(id);
        core::logInfo(
            "{}", core::formatMessage(messages::OverriddenId, id.str(),
                                      source != nullptr ? std::string_view{source->packId} : messages::UnknownPack));
    }
}

} // namespace dwe::content
