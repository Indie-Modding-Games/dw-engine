#pragma once

#include <dwe/assets/dev_asset_sources.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>

#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace dwe::content {

// The packs the game and dwe-sim read, mounted as one virtual file system. Everything is read by asset id through it,
// so a mods pack that overrides an id is what the game uses (docs/architecture/vfs.md).
class GameContent {
public:
    // Mounts the packs of the content folder and of its packs/ folder, the team's packs from the source tree when
    // those have no game pack, and the packs of each mods folder. With dev assets (off when empty, the default), the
    // packs of their extracted content folders whose ids no normal pack has are mounted under every normal pack, so
    // they only fill what the normal packs lack (selectDevPacks). Logs the stack, which packs come from the dev assets
    // and which were left out, and the ids that packs override.
    static core::Result<std::unique_ptr<GameContent>> mount(const std::filesystem::path& content,
                                                            std::span<const std::filesystem::path> mods,
                                                            const assets::DevAssetSources& devAssets = {});

    GameContent(const GameContent&) = delete;
    GameContent& operator=(const GameContent&) = delete;
    GameContent(GameContent&&) = delete;
    GameContent& operator=(GameContent&&) = delete;
    ~GameContent() = default;

    [[nodiscard]] const assets::VirtualFileSystem& files() const { return *files_; }
    // The roots of the mounted packs that come from the dev assets' extracted content folders; none without them.
    [[nodiscard]] std::vector<std::filesystem::path> devPacks() const;

private:
    GameContent() = default;

    // Logs each mounted pack, saying which come from the dev assets, and the ids that packs override.
    void logStack() const;

    // The VFS reads through the opened archives, which read through the disk: they live as long as it.
    assets::DiskFileSystem disk_;
    assets::OpenedPacks packs_;
    std::unique_ptr<assets::VirtualFileSystem> files_;
};

} // namespace dwe::content
