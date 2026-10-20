#pragma once

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>

#include <filesystem>
#include <memory>
#include <span>

namespace dwe::content {

// The packs the game and dwe-sim read, mounted as one virtual file system. Everything is read by asset id through it,
// so a mods pack that overrides an id is what the game uses (docs/architecture/vfs.md).
class GameContent {
public:
    // Mounts the packs of the content folder and of its packs/ folder, the team's packs from the source tree when
    // those have no game pack, and the packs of each mods folder. Logs the stack and the ids that packs override.
    static core::Result<std::unique_ptr<GameContent>> mount(const std::filesystem::path& content,
                                                            std::span<const std::filesystem::path> mods);

    GameContent(const GameContent&) = delete;
    GameContent& operator=(const GameContent&) = delete;
    GameContent(GameContent&&) = delete;
    GameContent& operator=(GameContent&&) = delete;
    ~GameContent() = default;

    [[nodiscard]] const assets::VirtualFileSystem& files() const { return *files_; }

private:
    GameContent() = default;

    // The VFS reads through the opened archives, which read through the disk: they live as long as it.
    assets::DiskFileSystem disk_;
    assets::OpenedPacks packs_;
    std::unique_ptr<assets::VirtualFileSystem> files_;
};

} // namespace dwe::content
