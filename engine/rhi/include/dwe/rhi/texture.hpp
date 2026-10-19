#pragma once

#include <dwe/core/error.hpp>
#include <dwe/rhi/device.hpp>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace dwe::rhi {

struct TextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // 8-bit RGBA, row by row from the top left: width * height * 4 bytes.
    std::span<const std::byte> pixels;
};

// A 2D image that shaders sample, in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL. It has no sampler of its own: the
// debug UI samples with its own. It may be dropped while frames that draw it are in flight, because its Vulkan
// objects go through Device::destroyLater, but it must not outlive the device.
class Texture {
public:
    // Uploads the pixels and waits for the copy to finish.
    static core::Result<Texture> create(Device& device, const TextureDesc& desc);

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;
    ~Texture();

    [[nodiscard]] VkImageView view() const { return view_; }
    [[nodiscard]] std::uint32_t width() const { return width_; }
    [[nodiscard]] std::uint32_t height() const { return height_; }

private:
    Texture(Device& device, std::uint32_t width, std::uint32_t height)
        : device_(&device), width_(width), height_(height) {}

    void release();

    Device* device_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

} // namespace dwe::rhi
