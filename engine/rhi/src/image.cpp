#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/rhi/buffer.hpp>
#include <dwe/rhi/image.hpp>

#include <vk_mem_alloc.h>

#include <array>
#include <cstring>
#include <format>
#include <utility>

#include "vulkan_util.hpp"

namespace dwe::rhi {
namespace {

VkImageUsageFlags toVulkan(ImageUsage usage) {
  VkImageUsageFlags flags = 0;
  if (hasAll(usage, ImageUsage::Sampled)) {
    // Sampled images are filled and updated by copies, and start cleared.
    flags |= VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }
  if (hasAll(usage, ImageUsage::ColorTarget)) {
    flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  }
  if (hasAll(usage, ImageUsage::DepthTarget)) {
    flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  }
  return flags;
}

} // namespace

core::Result<void> checkImageWrite(Extent extent, Format format, const Rect& region,
                                   std::size_t pixelBytes) {
  const std::uint32_t texelBytes = bytesPerPixel(format);
  if (texelBytes == 0) {
    return core::fail("only color images can be written from the CPU");
  }
  if (region.x < 0 || region.y < 0 || region.width == 0 || region.height == 0 ||
      std::uint64_t{static_cast<std::uint32_t>(region.x)} + region.width > extent.width ||
      std::uint64_t{static_cast<std::uint32_t>(region.y)} + region.height > extent.height) {
    return core::fail(std::format("the region {}x{} at ({}, {}) is not inside the {}x{} image",
                                  region.width, region.height, region.x, region.y, extent.width,
                                  extent.height));
  }
  if (const std::uint64_t expected = std::uint64_t{region.width} * region.height * texelBytes;
      pixelBytes != expected) {
    return core::fail(std::format("a {}x{} region needs {} bytes, not {}", region.width,
                                  region.height, expected, pixelBytes));
  }
  return {};
}

core::Result<Image> Image::create(Device& device, const ImageDesc& desc) {
  const NativeHandles gpu = device.nativeHandles();
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(gpu.physicalDevice, &properties);
  const std::uint32_t maxSide = properties.limits.maxImageDimension2D;
  if (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.width > maxSide ||
      desc.extent.height > maxSide) {
    return core::fail(std::format("an image has 1 to {} pixels on a side; this one is {}x{}",
                                  maxSide, desc.extent.width, desc.extent.height));
  }
  if (desc.format == Format::Undefined ||
      isDepthFormat(desc.format) != hasAll(desc.usage, ImageUsage::DepthTarget)) {
    return core::fail("depth images, and only they, are depth targets");
  }

  Image image{device, desc.extent, desc.format};
  const VkImageCreateInfo imageInfo{
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = toVulkan(desc.format),
      .extent = {.width = desc.extent.width, .height = desc.extent.height, .depth = 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = toVulkan(desc.usage),
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,
      .pQueueFamilyIndices = nullptr,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };

  VmaAllocationCreateInfo allocationInfo{};
  allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
  if (hasAll(desc.usage, ImageUsage::ColorTarget) || hasAll(desc.usage, ImageUsage::DepthTarget)) {
    // Render targets are recreated with the window: give them their own memory, as drivers prefer.
    allocationInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
  }
  if (const VkResult result = vmaCreateImage(device.allocator(), &imageInfo, &allocationInfo,
                                             &image.image_, &image.allocation_, nullptr);
      result != VK_SUCCESS) {
    image.device_ = nullptr;
    return core::fail(std::format("could not create a {}x{} image (Vulkan error {})",
                                  desc.extent.width, desc.extent.height, static_cast<int>(result)));
  }

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image.image_;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = imageInfo.format;
  viewInfo.subresourceRange = {.aspectMask = aspectOf(desc.format),
                               .baseMipLevel = 0,
                               .levelCount = 1,
                               .baseArrayLayer = 0,
                               .layerCount = 1};
  if (vkCreateImageView(gpu.device, &viewInfo, nullptr, &image.view_) != VK_SUCCESS) {
    return core::fail("could not create the view of an image");
  }

  if (hasAll(desc.usage, ImageUsage::Sampled) && desc.initialize) {
    auto cleared = submitAndWait(
        device, [&](VkCommandBuffer commands) { recordImageClear(commands, image.image_); });
    if (!cleared) {
      return std::unexpected{cleared.error()};
    }
  }
  return image;
}

core::Result<Image> Image::createTexture(Device& device, Extent extent,
                                         std::span<const std::byte> pixels) {
  if (auto valid = checkImageWrite(extent, Format::Rgba8Unorm,
                                   {.x = 0, .y = 0, .width = extent.width, .height = extent.height},
                                   pixels.size());
      !valid) {
    return std::unexpected{valid.error()};
  }
  auto image = create(
      device, {.extent = extent, .format = Format::Rgba8Unorm, .usage = ImageUsage::Sampled});
  if (!image) {
    return image;
  }
  if (auto written =
          image->write({.x = 0, .y = 0, .width = extent.width, .height = extent.height}, pixels);
      !written) {
    return std::unexpected{written.error()};
  }
  return image;
}

core::Result<void> Image::write(const Rect& region, std::span<const std::byte> pixels) {
  DWE_ASSERT(device_ != nullptr);
  if (auto valid = checkImageWrite(extent_, format_, region, pixels.size()); !valid) {
    return valid;
  }
  auto staging = Buffer::create(
      *device_,
      {.size = pixels.size(), .usage = BufferUsage::Storage, .location = MemoryLocation::Host});
  if (!staging) {
    return std::unexpected{staging.error()};
  }
  std::memcpy(staging->mapped().data(), pixels.data(), pixels.size());
  return submitAndWait(*device_, [&](VkCommandBuffer commands) {
    recordImageWrite(commands, image_, staging->native(), 0, region);
  });
}

Image::Image(Image&& other) noexcept
    : device_(std::exchange(other.device_, nullptr)),
      image_(std::exchange(other.image_, VK_NULL_HANDLE)),
      allocation_(std::exchange(other.allocation_, VK_NULL_HANDLE)),
      view_(std::exchange(other.view_, VK_NULL_HANDLE)), extent_(std::exchange(other.extent_, {})),
      format_(std::exchange(other.format_, Format::Undefined)) {}

Image& Image::operator=(Image&& other) noexcept {
  if (this != &other) {
    release();
    device_ = std::exchange(other.device_, nullptr);
    image_ = std::exchange(other.image_, VK_NULL_HANDLE);
    allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
    view_ = std::exchange(other.view_, VK_NULL_HANDLE);
    extent_ = std::exchange(other.extent_, {});
    format_ = std::exchange(other.format_, Format::Undefined);
  }
  return *this;
}

Image::~Image() {
  release();
}

void Image::release() {
  if (device_ == nullptr) {
    return;
  }
  device_->destroyLater([device = device_->nativeHandles().device, allocator = device_->allocator(),
                         image = image_, allocation = allocation_, view = view_] {
    vkDestroyImageView(device, view, nullptr);
    vmaDestroyImage(allocator, image, allocation);
  });
  device_ = nullptr;
}

Format swapchainFormat(const Device& device) {
  return fromVulkan(device.nativeHandles().swapchainFormat);
}

Format bestDepthFormat(const Device& device) {
  const VkPhysicalDevice physicalDevice = device.nativeHandles().physicalDevice;
  for (const Format format : std::array{Format::Depth32Float, Format::Depth24UnormX8}) {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice, toVulkan(format), &properties);
    if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
      return format;
    }
  }
  return Format::Depth16Unorm;
}

} // namespace dwe::rhi
