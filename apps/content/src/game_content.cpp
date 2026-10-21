#include <dwe/assets/dev_pack_selection.hpp>
#include <dwe/assets/lua_api.hpp>
#include <dwe/assets/pack_manifest.hpp>
#include <dwe/content/game_content.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "messages.hpp"

namespace dwe::content {
namespace {

// The folder of the content folder's extra packs, and the stem of the team's game pack.
constexpr std::string_view PacksFolder = "packs";
constexpr std::string_view GamePackName = "game";

// A mod that opened, waiting to be mounted.
struct ModCandidate {
    std::filesystem::path path;
    assets::PackLocation location;
    std::string lastError;
};

// The packs of the mods folders: those that opened, the archives they read from, and those that did not open.
struct OpenedMods {
    std::vector<ModCandidate> candidates;
    std::vector<std::unique_ptr<assets::PakFileSystem>> archives;
    std::vector<SkippedMod> skipped;
};

// The stack with every mod that mounts on it, and the mods that do not.
struct ModMounting {
    std::vector<assets::PackLocation> stack;
    std::vector<SkippedMod> skipped;
};

// The game's own packs: the content folder, its packs/ folder, and the team's packs when there is no game pack.
core::Result<std::vector<assets::FoundPack>> findGamePacks(const assets::FileSystem& disk,
                                                           const std::filesystem::path& content) {
    std::vector<std::filesystem::path> folders{content};
    std::error_code error;
    if (std::filesystem::is_directory(content / PacksFolder, error)) {
        folders.push_back(content / PacksFolder);
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
        std::ranges::any_of(found, [](const assets::FoundPack& pack) { return pack.path.stem() == GamePackName; });
    if (!hasGamePack) {
        if (auto added = add(DWE_TEAM_PACKS); !added) {
            return std::unexpected{added.error()};
        }
    }
    return found;
}

// Whether every mod of a stack declares a mod layer and every pack a Lua API the engine has. The manifests are the ones
// the stack mounted, so a pack is checked as it is used.
core::Result<void> checkMountedPacks(const assets::VirtualFileSystem& files) {
    for (const assets::MountedPack& pack : files.packs()) {
        if (pack.origin == assets::PackOrigin::Mod) {
            if (auto layer = assets::checkModLayer(pack.manifest); !layer) {
                return layer;
            }
        }
        if (auto compatible = assets::checkPackApi(pack.manifest); !compatible) {
            return compatible;
        }
    }
    return {};
}

// Mounts a stack and checks its packs' layers and Lua APIs.
core::Result<assets::VirtualFileSystem> mountChecked(std::span<const assets::PackLocation> locations) {
    auto files = assets::VirtualFileSystem::mount(locations);
    if (!files) {
        return std::unexpected{files.error()};
    }
    if (auto compatible = checkMountedPacks(*files); !compatible) {
        return std::unexpected{compatible.error()};
    }
    return files;
}

// Finds and opens the packs of the mods folders. A folder or archive that cannot be read is skipped.
OpenedMods openMods(const assets::FileSystem& disk, std::span<const std::filesystem::path> folders) {
    OpenedMods opened;
    for (const std::filesystem::path& folder : folders) {
        auto packs = assets::findPacks(disk, folder);
        if (!packs) {
            opened.skipped.push_back(SkippedMod{.path = folder, .reason = packs.error().message});
            continue;
        }
        for (const assets::FoundPack& pack : packs->packs) {
            auto opening = assets::openPacks(disk, std::span{&pack, 1}, assets::PackOrigin::Mod);
            if (!opening) {
                opened.skipped.push_back(SkippedMod{.path = pack.path, .reason = opening.error().message});
                continue;
            }
            // Its manifest is read and checked when it is mounted (checkMountedPacks).
            const assets::PackLocation& location = opening->locations.front();
            std::ranges::move(opening->archives, std::back_inserter(opened.archives));
            opened.candidates.push_back(ModCandidate{.path = pack.path, .location = location, .lastError = {}});
        }
    }
    return opened;
}

// Adds to the stack every mod that mounts with it, over as many rounds as it takes for mods that need other mods.
// The rest are skipped with the error of their last try. Mounting reads every pack's file list, so all the mods are
// tried at once first: one by one only when that fails.
ModMounting addMountableMods(std::vector<assets::PackLocation> stack, std::vector<ModCandidate> candidates) {
    std::vector<assets::PackLocation> everything = stack;
    std::ranges::transform(candidates, std::back_inserter(everything), &ModCandidate::location);
    if (mountChecked(everything)) {
        return ModMounting{.stack = std::move(everything), .skipped = {}};
    }
    bool added = true;
    while (added && !candidates.empty()) {
        added = false;
        std::vector<ModCandidate> waiting;
        for (ModCandidate& candidate : candidates) {
            stack.push_back(candidate.location);
            if (auto trial = mountChecked(stack); trial) {
                added = true;
            } else {
                stack.pop_back();
                candidate.lastError = trial.error().message;
                waiting.push_back(std::move(candidate));
            }
        }
        candidates = std::move(waiting);
    }
    ModMounting mounting{.stack = std::move(stack), .skipped = {}};
    for (ModCandidate& candidate : candidates) {
        mounting.skipped.push_back(
            SkippedMod{.path = std::move(candidate.path), .reason = std::move(candidate.lastError)});
    }
    return mounting;
}

void logShadowed(std::span<const assets::ShadowedDevPack> shadowed) {
    for (const assets::ShadowedDevPack& pack : shadowed) {
        core::logWarning("{}", core::formatMessage(messages::ShadowedDevPack, pack.id, assets::pathText(pack.pack),
                                                   assets::pathText(pack.winner)));
    }
}

// The game's packs, then the dev ones that their ids pick (selectDevPacks), which fill gaps. The mods play no part:
// one of them cannot change, or break, the game's own stack. A mod with a dev pack's id is then refused as a pack
// mounted twice, as it is without dev assets when it has the id of a game pack.
core::Result<std::vector<assets::PackLocation>> gameStack(std::span<const assets::PackLocation> game,
                                                          std::span<const assets::PackLocation> dev) {
    std::vector<assets::PackLocation> stack{game.begin(), game.end()};
    if (dev.empty()) {
        return stack;
    }
    auto selection = assets::selectDevPacks(game, dev);
    if (!selection) {
        return std::unexpected{selection.error()};
    }
    logShadowed(selection->shadowed);
    stack.insert(stack.end(), selection->packs.begin(), selection->packs.end());
    return stack;
}

} // namespace

core::Result<std::unique_ptr<GameContent>> GameContent::mount(const std::filesystem::path& content,
                                                              std::span<const std::filesystem::path> mods,
                                                              const assets::DevAssetSources& devAssets) {
    std::unique_ptr<GameContent> mounted{new GameContent};
    auto found = findGamePacks(mounted->disk_, content);
    if (!found) {
        return std::unexpected{found.error()};
    }
    const std::size_t gameCount = found->size();
    auto dev = assets::findDevPacks(mounted->disk_, devAssets.extractedContent);
    if (!dev) {
        return std::unexpected{dev.error()};
    }
    found->insert(found->end(), dev->begin(), dev->end());
    if (found->empty()) {
        return core::fail(core::formatMessage(messages::NoPacks, assets::pathText(content)));
    }
    // The content folder, the team's packs and the dev assets are the engine's own; the mods are not (openMods).
    auto opened = assets::openPacks(mounted->disk_, *found, assets::PackOrigin::Engine);
    if (!opened) {
        return std::unexpected{opened.error()};
    }
    mounted->packs_ = std::move(*opened);
    OpenedMods modPacks = openMods(mounted->disk_, mods);
    std::ranges::move(modPacks.archives, std::back_inserter(mounted->packs_.archives));
    const std::span<const assets::PackLocation> locations = mounted->packs_.locations;
    auto game = gameStack(locations.first(gameCount), locations.subspan(gameCount));
    if (!game) {
        return std::unexpected{game.error()};
    }
    // The game's own packs, with the dev packs that fill their gaps, must mount on their own: a mod cannot fix them.
    if (auto checked = mountChecked(*game); !checked) {
        return std::unexpected{checked.error()};
    }
    ModMounting mounting = addMountableMods(std::move(*game), std::move(modPacks.candidates));
    mounted->skippedMods_ = std::move(modPacks.skipped);
    std::ranges::move(mounting.skipped, std::back_inserter(mounted->skippedMods_));
    auto files = mountChecked(mounting.stack);
    if (!files) {
        return std::unexpected{files.error()};
    }
    mounted->packs_.locations = std::move(mounting.stack);
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
    for (const SkippedMod& mod : skippedMods_) {
        core::logError("{}", core::formatMessage(messages::ModSkipped, assets::pathText(mod.path), mod.reason));
    }
}

} // namespace dwe::content
