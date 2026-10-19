#include <dwe/assets/virtual_file_system.hpp>

#include <algorithm>
#include <cstdint>
#include <format>
#include <string_view>
#include <tuple>
#include <utility>

namespace dwe::assets {

namespace {

constexpr std::string_view ManifestName = "pack.yaml";

core::Result<MountedPack> readPack(const FileSystem& fileSystem, const std::filesystem::path& root) {
    const std::filesystem::path manifestPath = root / ManifestName;
    const auto bytes = fileSystem.readFile(manifestPath);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    const std::string_view text{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
    auto manifest = parsePackManifest(text);
    if (!manifest) {
        return core::fail(std::format("{}: {}", pathText(manifestPath), manifest.error().message));
    }
    return MountedPack{.manifest = std::move(*manifest), .root = root};
}

std::size_t indexOf(std::span<const MountedPack> packs, std::string_view id) {
    return static_cast<std::size_t>(
        std::ranges::find(packs, id, [](const MountedPack& pack) -> const std::string& { return pack.manifest.id; }) -
        packs.begin());
}

core::Result<void> checkDependencies(std::span<const MountedPack> packs) {
    for (const MountedPack& pack : packs) {
        const PackManifest& manifest = pack.manifest;
        for (const PackDependency& dependency : manifest.dependencies) {
            const std::size_t index = indexOf(packs, dependency.id);
            if (index == packs.size()) {
                return core::fail(
                    std::format("pack '{}' needs pack '{}', which is not mounted", manifest.id, dependency.id));
            }
            const PackManifest& found = packs[index].manifest;
            if (found.version < dependency.minVersion) {
                return core::fail(std::format("pack '{}' needs '{}' {} or newer, but {} is mounted", manifest.id,
                                              found.id, formatVersion(dependency.minVersion),
                                              formatVersion(found.version)));
            }
            if (found.layer > manifest.layer) {
                return core::fail(std::format("pack '{}' ({}) cannot depend on '{}', which is in the higher layer {}",
                                              manifest.id, layerName(manifest.layer), found.id,
                                              layerName(found.layer)));
            }
        }
    }
    return {};
}

enum class Visit : std::uint8_t { New, Open, Done };

// Depth-first search that fails on the first dependency it finds open on the current path.
core::Result<void> visit(std::span<const MountedPack> packs, std::size_t index, std::vector<Visit>& visits,
                         std::vector<std::string_view>& path) {
    visits[index] = Visit::Open;
    path.push_back(packs[index].manifest.id);
    for (const PackDependency& dependency : packs[index].manifest.dependencies) {
        const std::size_t next = indexOf(packs, dependency.id);
        if (visits[next] == Visit::Open) {
            std::string cycle;
            for (auto id = std::ranges::find(path, dependency.id); id != path.end(); ++id) {
                cycle += std::format("{} -> ", *id);
            }
            return core::fail(std::format("dependency cycle: {}{}", cycle, dependency.id));
        }
        if (visits[next] == Visit::New) {
            if (auto result = visit(packs, next, visits, path); !result) {
                return result;
            }
        }
    }
    path.pop_back();
    visits[index] = Visit::Done;
    return {};
}

core::Result<void> checkCycles(std::span<const MountedPack> packs) {
    std::vector<Visit> visits(packs.size(), Visit::New);
    std::vector<std::string_view> path;
    for (std::size_t index = 0; index < packs.size(); ++index) {
        if (visits[index] == Visit::New) {
            if (auto result = visit(packs, index, visits, path); !result) {
                return result;
            }
        }
    }
    return {};
}

auto stackKey(const MountedPack& pack) {
    return std::tuple{pack.manifest.layer, pack.manifest.priority};
}

// Orders packs sorted by id from the bottom of the stack up: by layer, then priority; packs that tie go after the
// packs of the tie they depend on, and otherwise by id.
std::vector<MountedPack> stack(std::vector<MountedPack> packs) {
    std::ranges::stable_sort(packs, {}, stackKey);
    std::vector<MountedPack> stacked;
    stacked.reserve(packs.size());
    for (auto groupStart = packs.begin(); groupStart != packs.end();) {
        const auto groupEnd = std::ranges::find_if(
            groupStart, packs.end(), [&](const auto& pack) { return stackKey(pack) != stackKey(*groupStart); });
        const std::span<MountedPack> group{groupStart, groupEnd};
        std::vector<bool> placed(group.size(), false);
        const auto isPlaced = [&](const PackDependency& dependency) {
            const std::size_t index = indexOf(group, dependency.id);
            return index == group.size() || placed[index];
        };
        std::vector<std::size_t> order;
        // Without cycles, every round places one pack.
        while (order.size() < group.size()) {
            for (std::size_t index = 0; index < group.size(); ++index) {
                if (!placed[index] && std::ranges::all_of(group[index].manifest.dependencies, isPlaced)) {
                    placed[index] = true;
                    order.push_back(index);
                    break;
                }
            }
        }
        for (const std::size_t index : order) {
            stacked.push_back(std::move(group[index]));
        }
        groupStart = groupEnd;
    }
    return stacked;
}

} // namespace

core::Result<VirtualFileSystem> VirtualFileSystem::mount(const FileSystem& fileSystem,
                                                         std::span<const std::filesystem::path> packRoots) {
    std::vector<MountedPack> packs;
    for (const std::filesystem::path& root : packRoots) {
        auto pack = readPack(fileSystem, root);
        if (!pack) {
            return std::unexpected{pack.error()};
        }
        packs.push_back(std::move(*pack));
    }
    std::ranges::stable_sort(packs, {}, [](const MountedPack& pack) -> const std::string& { return pack.manifest.id; });
    const auto twice = std::ranges::adjacent_find(
        packs, [](const MountedPack& a, const MountedPack& b) { return a.manifest.id == b.manifest.id; });
    if (twice != packs.end()) {
        return core::fail(std::format("pack '{}' is mounted twice: from '{}' and '{}'", twice->manifest.id,
                                      pathText(twice->root), pathText(std::next(twice)->root)));
    }
    if (auto checked = checkDependencies(packs); !checked) {
        return std::unexpected{checked.error()};
    }
    if (auto checked = checkCycles(packs); !checked) {
        return std::unexpected{checked.error()};
    }

    VirtualFileSystem vfs{fileSystem, stack(std::move(packs))};
    for (const MountedPack& pack : vfs.packs_) {
        if (auto indexed = vfs.indexPack(pack); !indexed) {
            return std::unexpected{indexed.error()};
        }
    }
    return vfs;
}

core::Result<void> VirtualFileSystem::indexPack(const MountedPack& pack) {
    auto files = fileSystem_->listFiles(pack.root);
    if (!files) {
        return std::unexpected{files.error()};
    }
    std::ranges::sort(*files);
    std::map<AssetId, std::string_view> packFiles;
    for (const std::string& file : *files) {
        if (file == ManifestName) {
            continue;
        }
        auto id = assetIdOfFile(file);
        if (!id) {
            return core::fail(std::format("pack '{}': {}", pack.manifest.id, id.error().message));
        }
        const auto [other, added] = packFiles.emplace(*id, file);
        if (!added) {
            return core::fail(std::format("pack '{}': '{}' and '{}' both give the asset id '{}'", pack.manifest.id,
                                          other->second, file, id->str()));
        }
        assets_[*id].push_back(AssetSource{.packId = pack.manifest.id, .file = pack.root / file});
    }
    return {};
}

const AssetSource* VirtualFileSystem::resolve(const AssetId& id) const {
    const auto found = assets_.find(id);
    return found == assets_.end() ? nullptr : &found->second.back();
}

core::Result<std::vector<std::byte>> VirtualFileSystem::read(const AssetId& id) const {
    const AssetSource* source = resolve(id);
    if (source == nullptr) {
        return core::fail(std::format("no pack has the asset '{}'", id.str()));
    }
    return fileSystem_->readFile(source->file);
}

std::vector<AssetId> VirtualFileSystem::ids() const {
    std::vector<AssetId> all;
    all.reserve(assets_.size());
    for (const auto& [id, sources] : assets_) {
        all.push_back(id);
    }
    return all;
}

std::span<const AssetSource> VirtualFileSystem::sources(const AssetId& id) const {
    const auto found = assets_.find(id);
    return found == assets_.end() ? std::span<const AssetSource>{} : std::span{found->second};
}

core::Result<std::vector<std::filesystem::path>> findPacks(const FileSystem& fileSystem,
                                                           const std::filesystem::path& folder) {
    const auto files = fileSystem.listFiles(folder);
    if (!files) {
        return std::unexpected{files.error()};
    }
    std::vector<std::filesystem::path> packs;
    for (const std::string& file : *files) {
        if (file == ManifestName) {
            return std::vector{folder};
        }
        const std::size_t slash = file.find('/');
        if (slash != std::string::npos && std::string_view{file}.substr(slash + 1) == ManifestName) {
            packs.push_back(folder / file.substr(0, slash));
        }
    }
    std::ranges::sort(packs);
    return packs;
}

std::vector<AssetId> VirtualFileSystem::overriddenIds() const {
    std::vector<AssetId> overridden;
    for (const auto& [id, sources] : assets_) {
        if (sources.size() > 1) {
            overridden.push_back(id);
        }
    }
    return overridden;
}

} // namespace dwe::assets
