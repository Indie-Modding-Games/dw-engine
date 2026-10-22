#pragma once

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

namespace dwe::game::field::testing {

// The packs of DWE_CONTENT (extracted with dwx) and the game pack of the source tree, mounted;
// nothing when DWE_CONTENT is not set.
inline std::optional<dwe::assets::VirtualFileSystem>
mountContent(const dwe::assets::DiskFileSystem& disk) {
  const std::optional<std::filesystem::path> mountedContent = dwe::test_support::userContent();
  if (!mountedContent) {
    return std::nullopt;
  }
  const std::filesystem::path& content = *mountedContent;
  auto packs = dwe::assets::findPacks(disk, content);
  if (!packs) {
    FAIL(packs.error().message);
    return std::nullopt;
  }
  // The extracted content is folders; the tests need no archives, which would have to outlive the
  // mount.
  std::vector<std::filesystem::path> roots;
  for (const dwe::assets::FoundPack& pack : packs->packs) {
    if (pack.kind == dwe::assets::FoundPack::Kind::Folder) {
      roots.push_back(pack.path);
    }
  }
  roots.emplace_back(DWE_GAME_PACK);
  auto vfs = dwe::assets::VirtualFileSystem::mount(disk, roots, dwe::assets::PackOrigin::Engine);
  if (!vfs) {
    FAIL(vfs.error().message);
    return std::nullopt;
  }
  return std::move(*vfs);
}

} // namespace dwe::game::field::testing
