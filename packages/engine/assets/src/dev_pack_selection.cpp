#include <dwe/assets/dev_pack_selection.hpp>

#include <map>

namespace dwe::assets {

core::Result<std::vector<FoundPack>> findDevPacks(const FileSystem& fileSystem,
                                                  std::span<const std::filesystem::path> folders) {
  std::vector<FoundPack> packs;
  for (const std::filesystem::path& folder : folders) {
    auto found = findPacks(fileSystem, folder);
    if (!found) {
      return std::unexpected{found.error()};
    }
    packs.insert(packs.end(), found->packs.begin(), found->packs.end());
  }
  return packs;
}

core::Result<DevPackSelection> selectDevPacks(std::span<const PackLocation> normal,
                                              std::span<const PackLocation> dev) {
  // Each id and the pack that holds it, the normal packs first.
  std::map<std::string, std::filesystem::path, std::less<>> taken;
  for (const PackLocation& pack : normal) {
    auto manifest = readPackManifest(pack);
    if (!manifest) {
      return std::unexpected{manifest.error()};
    }
    taken.try_emplace(manifest->id, pack.root);
  }
  DevPackSelection selection;
  for (const PackLocation& pack : dev) {
    auto manifest = readPackManifest(pack);
    if (!manifest) {
      return std::unexpected{manifest.error()};
    }
    const auto [holder, added] = taken.try_emplace(manifest->id, pack.root);
    if (added) {
      selection.packs.push_back({.fileSystem = pack.fileSystem,
                                 .root = pack.root,
                                 .fillsGaps = true,
                                 .origin = pack.origin});
    } else {
      selection.shadowed.push_back(
          {.id = manifest->id, .pack = pack.root, .winner = holder->second});
    }
  }
  return selection;
}

} // namespace dwe::assets
