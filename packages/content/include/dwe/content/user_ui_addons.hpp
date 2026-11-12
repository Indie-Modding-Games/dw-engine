#pragma once

#include <dwe/assets/file_system.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace dwe::content {

// The id of the pack that holds the UI addons the player copied into the user data folder.
inline constexpr std::string_view UserUiAddonsPack = "user_ui_addons";

// The UI addons the player installs by copying a folder into <user data>/ui/addons/
// (docs/modding/interfaces.md), seen as a pack: its root is the user data folder, its only files
// are the ones under ui/addons/, and its pack.yaml is made here (a user layer pack on top of the
// game's packs, on the Lua API the engine has), so the player writes no manifest but each addon's
// own addon.yaml. Nothing else of the user data folder (saves, settings) is in it.
class UserUiAddonsFileSystem final : public assets::FileSystem {
public:
  explicit UserUiAddonsFileSystem(std::filesystem::path root);

  // Whether the folder holds any addon file, so an empty one adds no pack.
  [[nodiscard]] bool hasAddons() const;
  [[nodiscard]] const std::filesystem::path& root() const { return root_; }

  [[nodiscard]] core::Result<std::vector<std::string>>
  listFiles(const std::filesystem::path& folder) const override;
  [[nodiscard]] core::Result<std::vector<std::byte>>
  readFile(const std::filesystem::path& file) const override;
  [[nodiscard]] core::Result<std::uint64_t>
  fileSize(const std::filesystem::path& file) const override;
  [[nodiscard]] core::Result<std::unique_ptr<assets::FileReader>>
  openFile(const std::filesystem::path& file) const override;

private:
  // The path of a file under ui/addons/ relative to the root, or empty for any other file.
  [[nodiscard]] std::string addonFile(const std::filesystem::path& file) const;

  std::filesystem::path root_;
  std::string manifest_;
  assets::DiskFileSystem disk_;
};

} // namespace dwe::content
