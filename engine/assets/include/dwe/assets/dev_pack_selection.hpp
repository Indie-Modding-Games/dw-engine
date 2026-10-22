#pragma once

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace dwe::assets {

// A pack of an extracted content folder left out because a pack with its id wins over it.
struct ShadowedDevPack {
  std::string id;
  std::filesystem::path pack;
  std::filesystem::path winner;

  friend bool operator==(const ShadowedDevPack&, const ShadowedDevPack&) = default;
};

// The packs of the extracted content folders to mount with the normal ones, and those left out.
struct DevPackSelection {
  std::vector<PackLocation> packs;
  std::vector<ShadowedDevPack> shadowed;
};

// The packs of each extracted content folder (findPacks), folder after folder.
core::Result<std::vector<FoundPack>> findDevPacks(const FileSystem& fileSystem,
                                                  std::span<const std::filesystem::path> folders);

// Picks the dev packs to mount by the ids of their manifests: a normal pack wins over a dev pack
// with its id, and an earlier dev pack over a later one. The picked packs fill gaps
// (PackLocation::fillsGaps): they stack under every normal pack, so they only provide what the
// normal packs lack. The left-out ones are reported, never dropped in silence. Fails on a manifest
// that cannot be read.
core::Result<DevPackSelection> selectDevPacks(std::span<const PackLocation> normal,
                                              std::span<const PackLocation> dev);

} // namespace dwe::assets
