#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pack_manifest.hpp>

#include <cstddef>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace dwe::assets {

// A pack in the stack of the virtual file system.
struct MountedPack {
    PackManifest manifest;
    std::filesystem::path root;
};

// A file that provides an asset, and the pack it belongs to.
struct AssetSource {
    std::string packId;
    std::filesystem::path file;
};

// The assets of a set of packs, stacked by layer and priority, by name. When several packs have the same asset id,
// the one highest in the stack wins; an id that only one pack has is simply added.
class VirtualFileSystem {
public:
    // Reads the manifests and lists the files of the packs at these folders, checks their dependencies and stacks
    // them. The file system must outlive the result.
    static core::Result<VirtualFileSystem> mount(const FileSystem& fileSystem,
                                                 std::span<const std::filesystem::path> packRoots);

    // The packs, from the bottom of the stack to the top.
    [[nodiscard]] std::span<const MountedPack> packs() const { return packs_; }

    // The file that provides an asset, or nullptr if no pack has it.
    [[nodiscard]] const AssetSource* resolve(const AssetId& id) const;

    // Reads the bytes of an asset from the pack that wins.
    [[nodiscard]] core::Result<std::vector<std::byte>> read(const AssetId& id) const;

    // Every asset id, sorted.
    [[nodiscard]] std::vector<AssetId> ids() const;

    // Every file that provides an asset, from the bottom of the stack up: the last one wins.
    [[nodiscard]] std::span<const AssetSource> sources(const AssetId& id) const;

    // The ids that more than one pack provides, sorted. For debugging overrides.
    [[nodiscard]] std::vector<AssetId> overriddenIds() const;

private:
    VirtualFileSystem(const FileSystem& fileSystem, std::vector<MountedPack> packs)
        : fileSystem_(&fileSystem), packs_(std::move(packs)) {}

    core::Result<void> indexPack(const MountedPack& pack);

    const FileSystem* fileSystem_;
    std::vector<MountedPack> packs_;
    std::map<AssetId, std::vector<AssetSource>> assets_;
};

// The packs in a content folder, sorted: the folder itself when it has a pack.yaml, otherwise each folder right
// under it that has one. A folder without packs gives none; one that cannot be listed is an error.
core::Result<std::vector<std::filesystem::path>> findPacks(const FileSystem& fileSystem,
                                                           const std::filesystem::path& folder);

} // namespace dwe::assets
