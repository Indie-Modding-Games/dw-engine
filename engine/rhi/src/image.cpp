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

VkImageUsageFlags toVulkan(ImageUsage usage, bool mipmapped) {
  VkImageUsageFlags flags = 0;
  if (hasAll(usage, ImageUsage::Sampled)) {
    // Sampled images are filled and updated by copies, and start cleared; a mip chain is blitted
    // from level to level.
    flags |= VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (mipmapped) {
      flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
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

std::uint32_t mipLevelCount(Extent extent) {
  std::uint32_t levels = 1;
  for (std::uint32_t side = std::max(extent.width, extent.height); side > 1; side /= 2) {
    ++levels;
  }
  return levels;
}

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

  const bool mipmapped =
      desc.mipmapped && hasAll(desc.usage, ImageUsage::Sampled) && !isDepthFormat(desc.format);
  const bool target =
      hasAll(desc.usage, ImageUsage::ColorTarget) || hasAll(desc.usage, ImageUsage::DepthTarget);
  if (desc.samples != 1 && (!target || hasAll(desc.usage, ImageUsage::Sampled) ||
                            (desc.samples != 2 && desc.samples != 4 && desc.samples != 8))) {
    return core::fail("only render targets that are not sampled take 2, 4 or 8 samples per pixel");
  }
  Image image{device, desc.extent, desc.format, mipmapped ? mipLevelCount(desc.extent) : 1};
  const VkImageCreateInfo imageInfo{
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = toVulkan(desc.format),
      .extent = {.width = desc.extent.width, .height = desc.extent.height, .depth = 1},
      .mipLevels = image.mipLevels_,
      .arrayLayers = 1,
      .samples = static_cast<VkSampleCountFlagBits>(desc.samples),
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = toVulkan(desc.usage, mipmapped),
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,
      .pQueueFamilyIndices = nullptr,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };

  VmaAllocationCreateInfo allocationInfo{};
  allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
  if (target) {
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
                               .levelCount = image.mipLevels_,
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
                                         std::span<const std::byte> pixels, bool mipmapped) {
  if (auto valid = checkImageWrite(extent, Format::Rgba8Unorm,
                                   {.x = 0, .y = 0, .width = extent.width, .height = extent.height},
                                   pixels.size());
      !valid) {
    return std::unexpected{valid.error()};
  }
  auto image = create(device, {.extent = extent,
                               .format = Format::Rgba8Unorm,
                               .usage = ImageUsage::Sampled,
                               .mipmapped = mipmapped});
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
    recordImageWrite(commands, image_, staging->native(), 0, region, extent_, mipLevels_);
  });
}

Image::Image(Image&& other) noexcept
    : device_(std::exchange(other.device_, nullptr)),
      image_(std::exchange(other.image_, VK_NULL_HANDLE)),
      allocation_(std::exchange(other.allocation_, VK_NULL_HANDLE)),
      view_(std::exchange(other.view_, VK_NULL_HANDLE)), extent_(std::exchange(other.extent_, {})),
      format_(std::exchange(other.format_, Format::Undefined)),
      mipLevels_(std::exchange(other.mipLevels_, 1U)) {}

Image& Image::operator=(Image&& other) noexcept {
  if (this != &other) {
    release();
    device_ = std::exchange(other.device_, nullptr);
    image_ = std::exchange(other.image_, VK_NULL_HANDLE);
    allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
    view_ = std::exchange(other.view_, VK_NULL_HANDLE);
    extent_ = std::exchange(other.extent_, {});
    format_ = std::exchange(other.format_, Format::Undefined);
    mipLevels_ = std::exchange(other.mipLevels_, 1U);
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

std::uint32_t maxSampleCount(const Device& device, std::uint32_t wanted) {
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(device.nativeHandles().physicalDevice, &properties);
  const VkSampleCountFlags supported = properties.limits.framebufferColorSampleCounts &
                                       properties.limits.framebufferDepthSampleCounts;
  for (const std::uint32_t samples : {8U, 4U, 2U}) {
    if (samples <= wanted && (supported & samples) != 0) {
      return samples;
    }
  }
  return 1;
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
