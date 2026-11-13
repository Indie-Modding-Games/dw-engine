#pragma once

#include <dwe/assets/dev_asset_sources.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/content/user_ui_addons.hpp>
#include <dwe/core/error.hpp>

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace dwe::content {

// A mod that mounting left out, and why: for the player's list of mods.
struct SkippedMod {
  // The pack's folder or archive, or the mods folder when the folder itself could not be read.
  std::filesystem::path path;
  std::string reason;
};

// The packs the game and dwe-sim read, mounted as one virtual file system. Everything is read by
// asset id through it, so a mods pack that overrides an id is what the game uses
// (docs/architecture/vfs.md).
class GameContent {
public:
  // Mounts the packs of the content folder and of its packs/ folder, the team's packs
  // (teamPacksFolder: a package's or the source tree's) when those have no game pack, and the packs
  // of each mods folder. With dev assets (off when empty, the default), the packs of their
  // extracted content folders whose ids no normal pack has are mounted under every normal pack, so
  // they only fill what the normal packs lack (selectDevPacks). The game's own packs, with the dev
  // packs that fill their gaps, must all mount and declare a Lua API the engine has
  // (assets::checkPackApi). A mod that cannot (a broken manifest, a missing dependency, another
  // API) is left out and logged, and the rest still mount. Logs the stack, which packs come from
  // the dev assets and which were left out, the mods left out, and the ids that packs override.
  // With a user data folder, the UI addons the player copied into its ui/addons/, as folders or
  // .zip archives, mount as one more mod (UserUiAddonsFileSystem).
  static core::Result<std::unique_ptr<GameContent>>
  mount(const std::filesystem::path& content, std::span<const std::filesystem::path> mods,
        const assets::DevAssetSources& devAssets = {}, const std::filesystem::path& userData = {});

  GameContent(const GameContent&) = delete;
  GameContent& operator=(const GameContent&) = delete;
  GameContent(GameContent&&) = delete;
  GameContent& operator=(GameContent&&) = delete;
  ~GameContent() = default;

  [[nodiscard]] const assets::VirtualFileSystem& files() const { return *files_; }
  // Lists the mounted packs' files again (assets::VirtualFileSystem::rescan): the files a tool
  // added to or removed from them while the game runs are seen; the packs stay the same. The
  // reference files() gave stays valid.
  core::Result<void> rescan() { return files_->rescan(); }
  // The roots of the mounted packs that come from the dev assets' extracted content folders; none
  // without them.
  [[nodiscard]] std::vector<std::filesystem::path> devPacks() const;

  // The mods that mount left out, in the order they were found.
  [[nodiscard]] std::span<const SkippedMod> skippedMods() const { return skippedMods_; }
  // The .zip archives of the player's UI addons folder that give no addon, sorted by name; none
  // without a user data folder. mount logs the ones another addon of their id shadows; the rest are
  // for the game to say, as the addons that do not read.
  [[nodiscard]] std::span<const LeftOutUiAddonArchive> leftOutUiAddonArchives() const {
    return userUi_ ? userUi_->leftOutArchives() : std::span<const LeftOutUiAddonArchive>{};
  }

private:
  GameContent() = default;

  // Logs each mounted pack, saying which come from the dev assets, the ids that packs override, and
  // the mods left out.
  void logStack() const;

  // The VFS reads through the opened archives, which read through the disk: they live as long as
  // it.
  assets::DiskFileSystem disk_;
  std::unique_ptr<UserUiAddonsFileSystem> userUi_;
  assets::OpenedPacks packs_;
  std::unique_ptr<assets::VirtualFileSystem> files_;
  std::vector<SkippedMod> skippedMods_;
};

} // namespace dwe::content
