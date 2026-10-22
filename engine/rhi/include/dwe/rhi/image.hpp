#pragma once

#include <dwe/core/error.hpp>
#include <dwe/rhi/device.hpp>
#include <dwe/rhi/types.hpp>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <span>

VK_DEFINE_HANDLE(VmaAllocation)

namespace dwe::rhi {

enum class ImageUsage : std::uint8_t {
  // Sampled by shaders through the bindless table. Sampled images rest in
  // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL.
  Sampled = 1U << 0U,
  // Drawn into as a color attachment.
  ColorTarget = 1U << 1U,
  // Drawn into as the depth attachment.
  DepthTarget = 1U << 2U,
};

template <>
inline constexpr bool IsFlagEnum<ImageUsage> = true;

struct ImageDesc {
  Extent extent{};
  Format format = Format::Rgba8Unorm;
  ImageUsage usage = ImageUsage::Sampled;
  // Sampled images only: clear to transparent black now, waiting for the GPU. Without it the image
  // starts undefined and must be recorded through CommandList::initializeImage before anything
  // samples it.
  bool initialize = true;
};

// A 2D image in GPU memory with one mip level, and its view. Sampled images start cleared to
// transparent black. Render targets start in an undefined layout; whoever draws into them moves
// them to the attachment layout each frame. It may be dropped while frames that use it are in
// flight (its Vulkan objects go through Device::destroyLater), but it must not outlive the device.
class Image {
public:
  static core::Result<Image> create(Device& device, const ImageDesc& desc);

  // Creates a sampled Rgba8Unorm image holding pixels (width * height * 4 bytes, rows from the top)
  // and waits for the upload.
  static core::Result<Image> createTexture(Device& device, Extent extent,
                                           std::span<const std::byte> pixels);

  Image(const Image&) = delete;
  Image& operator=(const Image&) = delete;
  Image(Image&& other) noexcept;
  Image& operator=(Image&& other) noexcept;
  ~Image();

  // Replaces a region of a sampled color image with pixels (region.width * region.height texels,
  // rows from the top, tightly packed) and waits for the copy. Frames submitted before see the old
  // pixels, later ones the new.
  core::Result<void> write(const Rect& region, std::span<const std::byte> pixels);

  [[nodiscard]] VkImage native() const { return image_; }
  [[nodiscard]] VkImageView view() const { return view_; }
  [[nodiscard]] Extent extent() const { return extent_; }
  [[nodiscard]] Format format() const { return format_; }

private:
  Image(Device& device, Extent extent, Format format)
      : device_(&device), extent_(extent), format_(format) {}

  void release();

  Device* device_ = nullptr;
  VkImage image_ = VK_NULL_HANDLE;
  VmaAllocation allocation_ = VK_NULL_HANDLE;
  VkImageView view_ = VK_NULL_HANDLE;
  Extent extent_;
  Format format_ = Format::Undefined;
};

// The best depth format the GPU can draw into: Depth32Float, else Depth24UnormX8, else Depth16Unorm
// (which every GPU supports).
Format bestDepthFormat(const Device& device);

// The format of the device's swapchain images, which frames draw into.
Format swapchainFormat(const Device& device);

// Checks that region lies inside an image of the given extent and that pixels holds exactly its
// texels.
core::Result<void> checkImageWrite(Extent extent, Format format, const Rect& region,
                                   std::size_t pixelBytes);

} // namespace dwe::rhi
