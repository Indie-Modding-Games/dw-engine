#pragma once

#include <dwe/assets/file_system.hpp>
#include <dwe/content/zip_archive.hpp>

#include <cstddef>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::content {

// The id of the pack that holds the UI addons the player copied into the user data folder.
inline constexpr std::string_view UserUiAddonsPack = "user_ui_addons";

// A .zip of <user data>/ui/addons/ that gives no addon, and why.
struct LeftOutUiAddonArchive {
  // The asset folder of the addon it would give, ui/addons/<id>; ui/addons/<archive name> when its
  // id cannot be read.
  std::string folder;
  // Why, after the archive's path.
  std::string reason;
  // Whether an addon of its id is used instead: a folder (always preferred) or a .zip before it
  // by name.
  bool shadowed = false;
};

// The UI addons the player installs by copying a folder, or a .zip of one, into
// <user data>/ui/addons/ (docs/modding/interfaces.md), seen as a pack: its root is the user data
// folder, its only files are the ones under ui/addons/, and its pack.yaml is made here (a user
// layer pack on top of the game's packs, on the Lua API the engine has), so the player writes no
// manifest but each addon's own addon.yaml. Nothing else of the user data folder (saves,
// settings) is in it.
//
// A .zip is read in place, never unpacked: it holds addon.yaml at its root or in its only folder,
// and its files show as ui/addons/<id>/..., the id its addon.yaml declares, as if it were that
// folder (so the addon still reads only its own files). The archives are looked at once, when this
// is made, and checked whole (ZipArchive, ZipLimits); one that is not safe, has no addon.yaml with
// a valid id or a file that cannot be an asset gives no addon, nor does one whose id a folder or a
// .zip before it by name already gives (leftOutArchives()).
class UserUiAddonsFileSystem final : public assets::FileSystem {
public:
  explicit UserUiAddonsFileSystem(std::filesystem::path root, const ZipLimits& limits = {});

  // Whether the folder holds any addon file or usable .zip, so an empty one adds no pack.
  [[nodiscard]] bool hasAddons() const;
  [[nodiscard]] const std::filesystem::path& root() const { return root_; }
  // The .zip archives that give no addon, sorted by name.
  [[nodiscard]] std::span<const LeftOutUiAddonArchive> leftOutArchives() const { return leftOut_; }

  [[nodiscard]] core::Result<std::vector<std::string>>
  listFiles(const std::filesystem::path& folder) const override;
  [[nodiscard]] core::Result<std::vector<std::byte>>
  readFile(const std::filesystem::path& file) const override;
  [[nodiscard]] core::Result<std::uint64_t>
  fileSize(const std::filesystem::path& file) const override;
  [[nodiscard]] core::Result<std::unique_ptr<assets::FileReader>>
  openFile(const std::filesystem::path& file) const override;

private:
  // A file of an addon read from a .zip: the archive and its entry.
  struct ArchiveFile {
    std::size_t archive = 0;
    std::size_t entry = 0;
  };

  // Opens the .zip archives of ui/addons/, sorted by name, next to the folders there.
  void openArchives(const ZipLimits& limits);
  // The path of a file under ui/addons/ relative to the root, or empty for any other file (a .zip
  // of ui/addons/ itself is not one).
  [[nodiscard]] std::string addonFile(const std::filesystem::path& file) const;
  // The archive file a path under ui/addons/ names, or nullptr for a file on disk.
  [[nodiscard]] const ArchiveFile* archiveFile(std::string_view addonFile) const;

  std::filesystem::path root_;
  std::string manifest_;
  assets::DiskFileSystem disk_;
  std::vector<ZipArchive> archives_;
  // By path relative to the root (ui/addons/<id>/...).
  std::map<std::string, ArchiveFile, std::less<>> archiveFiles_;
  std::vector<LeftOutUiAddonArchive> leftOut_;
};

} // namespace dwe::content
