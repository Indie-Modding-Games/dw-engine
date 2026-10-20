#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/rhi/device.hpp>
#include <dwe/rhi/texture.hpp>

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::debug {

// The overlay's asset browser: mounts the packs of a content folder, lists their asset ids by their first segment
// and shows the PNG textures, with the pack that provides each asset and the packs it overrides.
class AssetViewer {
public:
    explicit AssetViewer(rhi::Device& device) : device_(&device) {}

    AssetViewer(const AssetViewer&) = delete;
    AssetViewer& operator=(const AssetViewer&) = delete;
    AssetViewer(AssetViewer&&) = delete;
    AssetViewer& operator=(AssetViewer&&) = delete;
    ~AssetViewer();

    // Mounts the packs of a folder in place of the ones mounted before, and logs what it found.
    void mount(const std::filesystem::path& folder);

    // Builds the window; open turns false when it is closed.
    void draw(bool& open);

private:
    struct Group {
        std::string name;
        // The range of visible_ that belongs to the group.
        std::size_t begin;
        std::size_t end;
    };

    struct Preview {
        rhi::Texture texture;
        VkDescriptorSet descriptor;
        std::size_t fileBytes;
        std::string_view stored; // how the file stores the texture
    };

    void applyFilter();
    void select(const assets::VirtualFileSystem& vfs, const assets::AssetId& id);
    void releasePreview();
    void drawList(const assets::VirtualFileSystem& vfs);
    void drawDetails(const assets::VirtualFileSystem& vfs);

    rhi::Device* device_;
    assets::DiskFileSystem disk_;
    // The .pak archives of the mounted packs; vfs_ reads through them, so it is reset first.
    std::vector<std::unique_ptr<assets::PakFileSystem>> archives_;
    std::array<char, 512> folder_{};
    std::optional<assets::VirtualFileSystem> vfs_;
    std::string status_;
    std::vector<assets::AssetId> ids_;

    std::array<char, 128> filter_{};
    // Indices into ids_ of the ids that pass the filter, by group.
    std::vector<std::size_t> visible_;
    std::vector<Group> groups_;
    bool expandGroups_ = false;

    std::optional<assets::AssetId> selected_;
    std::optional<Preview> preview_;
    std::string previewError_;
    int zoom_ = 1;
};

} // namespace dwe::debug
