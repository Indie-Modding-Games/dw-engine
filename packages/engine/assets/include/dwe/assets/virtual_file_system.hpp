#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pack_manifest.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {

// Where a pack is: its root folder in a file system. On disk that is the pack's folder; in a .pak
// archive opened with PakFileSystem it is the archive's path.
struct PackLocation {
  const FileSystem* fileSystem = nullptr;
  std::filesystem::path root;
  // A pack of the dev assets (findDevPacks): it stacks under every other pack, whatever its layer
  // and priority, so it only provides the ids that no other pack has.
  bool fillsGaps = false;
  PackOrigin origin = PackOrigin::Mod;
};

// A pack in the stack of the virtual file system.
struct MountedPack {
  PackManifest manifest;
  std::filesystem::path root;
  const FileSystem* fileSystem = nullptr;
  // As in its PackLocation.
  bool fillsGaps = false;
  PackOrigin origin = PackOrigin::Mod;
};

// A file of a pack and the asset id it gives.
struct PackFile {
  AssetId id;
  std::string path; // relative to the pack's root, with '/' between folders
};

// A file that provides an asset, and the pack it belongs to.
struct AssetSource {
  std::string packId;
  std::filesystem::path file;
};

// Reads and checks the pack.yaml at a pack's root; the error names the file.
core::Result<PackManifest> readPackManifest(const PackLocation& location);

// The assets of a set of packs, stacked by layer and priority, by name. When several packs have the
// same asset id, the one highest in the stack wins; an id that only one pack has is simply added.
class VirtualFileSystem {
public:
  // Reads the manifests and lists the files of the packs at these folders, checks their
  // dependencies and stacks them, all with one origin. The file system must outlive the result.
  static core::Result<VirtualFileSystem> mount(const FileSystem& fileSystem,
                                               std::span<const std::filesystem::path> packRoots,
                                               PackOrigin origin = PackOrigin::Mod);

  // The same for packs that may each be in a different file system, such as folders on disk and
  // .pak archives. Every file system must outlive the result.
  static core::Result<VirtualFileSystem> mount(std::span<const PackLocation> locations);

  // The packs, from the bottom of the stack to the top.
  [[nodiscard]] std::span<const MountedPack> packs() const { return packs_; }

  // The file that provides an asset, or nullptr if no pack has it.
  [[nodiscard]] const AssetSource* resolve(const AssetId& id) const;

  // Reads the bytes of an asset from the pack that wins.
  [[nodiscard]] core::Result<std::vector<std::byte>> read(const AssetId& id) const;
  // Opens the winning file of an asset to read it in pieces (FileSystem::openFile).
  [[nodiscard]] core::Result<std::unique_ptr<FileReader>> open(const AssetId& id) const;
  // The size in bytes of the file read() would return, without reading it.
  [[nodiscard]] core::Result<std::uint64_t> size(const AssetId& id) const;

  // Every asset id, sorted.
  [[nodiscard]] std::vector<AssetId> ids() const;

  // Every file that provides an asset, from the bottom of the stack up: the last one wins.
  [[nodiscard]] std::span<const AssetSource> sources(const AssetId& id) const;

  // The pack a source of sources() belongs to.
  [[nodiscard]] const MountedPack& packOf(const AssetSource& source) const;
  // Reads one source of an asset, winning or not: the files under a player's image replace only
  // how an image is drawn (Layer::User), so its loader still reads the game's own image.
  [[nodiscard]] core::Result<std::vector<std::byte>> read(const AssetSource& source) const;

  // The ids that more than one pack provides, sorted. For debugging overrides.
  [[nodiscard]] std::vector<AssetId> overriddenIds() const;

  // Lists the files of the mounted packs again, so files added to or removed from a pack's folder
  // since the mount are seen (a map editor writing a new document while the game runs). The packs
  // and their stack stay the same. On an error (a file that no longer gives a valid id) nothing
  // changes. The pointers resolve() gave and the spans sources() gave are invalid afterwards.
  core::Result<void> rescan();

private:
  explicit VirtualFileSystem(std::vector<MountedPack> packs) : packs_(std::move(packs)) {}

  core::Result<void> indexPack(const MountedPack& pack);

  std::vector<MountedPack> packs_;
  std::map<AssetId, std::vector<AssetSource>> assets_;
};

// Lists the asset files of a pack, sorted by id, leaving out its pack.yaml. Every file must give a
// valid asset id and no two files may give the same one.
core::Result<std::vector<PackFile>> listPackFiles(const FileSystem& fileSystem,
                                                  const std::filesystem::path& root);

// The extension of a pack in a single file, a .pak archive (pak.hpp).
inline constexpr std::string_view PakExtension = ".pak";

// A pack that findPacks found.
struct FoundPack {
  enum class Kind : std::uint8_t {
    Folder,  // a folder with a pack.yaml
    Archive, // a .pak file, opened with PakFileSystem
  };
  std::filesystem::path path;
  Kind kind = Kind::Folder;

  friend bool operator==(const FoundPack&, const FoundPack&) = default;
};

// What findPacks found in a content folder.
struct PackSearch {
  // Sorted by path.
  std::vector<FoundPack> packs;
  // Archives left out because a folder pack has the same name, sorted.
  std::vector<std::filesystem::path> hidden;
};

// The packs in a content folder. When the folder has a pack.yaml it is the only pack. Otherwise
// every folder right under it with a pack.yaml is a pack, and so is every .pak file right under it,
// except that a folder wins over the archive with its name: with both base/ and base.pak, base/ is
// mounted and base.pak is reported as hidden, since the folder is what one edits and the archive is
// built from it. Packs whose names differ but whose pack.yaml give the same id are all returned,
// and mounting them fails. A folder without packs gives none; one that cannot be listed is an
// error.
core::Result<PackSearch> findPacks(const FileSystem& fileSystem,
                                   const std::filesystem::path& folder);

} // namespace dwe::assets
