#include <dwe/rhi/frame_capture.hpp>

#include <vk_mem_alloc.h>

#include <cstring>
#include <format>
#include <utility>

#include "transition.hpp"

namespace dwe::rhi {

core::Result<FrameCapture> FrameCapture::record(Device& device, const Frame& frame) {
  if (!device.canCaptureFrames()) {
    return core::fail("this GPU cannot copy the window's images");
  }
  const VkFormat format = device.nativeHandles().swapchainFormat;
  if (format != VK_FORMAT_B8G8R8A8_UNORM && format != VK_FORMAT_R8G8B8A8_UNORM &&
      format != VK_FORMAT_B8G8R8A8_SRGB && format != VK_FORMAT_R8G8B8A8_SRGB) {
    return core::fail(
        std::format("cannot read back swapchain format {}", static_cast<int>(format)));
  }
  const std::size_t size = std::size_t{frame.extent.width} * frame.extent.height * 4;
  if (size == 0) {
    return core::fail("the frame has no pixels");
  }
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VmaAllocationCreateInfo allocationInfo{};
  allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
  allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
  FrameCapture capture{device};
  if (const VkResult result = vmaCreateBuffer(device.allocator(), &bufferInfo, &allocationInfo,
                                              &capture.buffer_, &capture.allocation_, nullptr);
      result != VK_SUCCESS) {
    capture.device_ = nullptr;
    return core::fail(
        std::format("could not create a readback buffer of {} bytes (Vulkan error {})", size,
                    static_cast<int>(result)));
  }
  capture.format_ = format;
  capture.width_ = frame.extent.width;
  capture.height_ = frame.extent.height;

  // Device::endFrame expects the image back in the color attachment layout.
  transitionImage(frame.commands, frame.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                  VK_ACCESS_2_TRANSFER_READ_BIT);
  VkBufferImageCopy region{};
  region.imageSubresource = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
  region.imageExtent = {.width = frame.extent.width, .height = frame.extent.height, .depth = 1};
  vkCmdCopyImageToBuffer(frame.commands, frame.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         capture.buffer_, 1, &region);
  // The copy must be visible to the host before it reads the buffer, after the frame's fence.
  VkBufferMemoryBarrier2 toHost{};
  toHost.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
  toHost.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
  toHost.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
  toHost.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
  toHost.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
  toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toHost.buffer = capture.buffer_;
  toHost.offset = 0;
  toHost.size = VK_WHOLE_SIZE;
  VkDependencyInfo dependency{};
  dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependency.bufferMemoryBarrierCount = 1;
  dependency.pBufferMemoryBarriers = &toHost;
  vkCmdPipelineBarrier2(frame.commands, &dependency);
  transitionImage(frame.commands, frame.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                  VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
  return capture;
}

FrameCapture::FrameCapture(FrameCapture&& other) noexcept
    : device_(std::exchange(other.device_, nullptr)),
      buffer_(std::exchange(other.buffer_, VK_NULL_HANDLE)),
      allocation_(std::exchange(other.allocation_, VK_NULL_HANDLE)), format_(other.format_),
      width_(other.width_), height_(other.height_) {}

FrameCapture& FrameCapture::operator=(FrameCapture&& other) noexcept {
  if (this != &other) {
    release();
    device_ = std::exchange(other.device_, nullptr);
    buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
    allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
    format_ = other.format_;
    width_ = other.width_;
    height_ = other.height_;
  }
  return *this;
}

FrameCapture::~FrameCapture() {
  release();
}

void FrameCapture::release() {
  if (device_ == nullptr || buffer_ == VK_NULL_HANDLE) {
    return;
  }
  device_->destroyLater(
      [allocator = device_->allocator(), buffer = buffer_, allocation = allocation_] {
        vmaDestroyBuffer(allocator, buffer, allocation);
      });
  buffer_ = VK_NULL_HANDLE;
  allocation_ = VK_NULL_HANDLE;
}

core::Result<CapturedImage> FrameCapture::read() {
  if (device_ == nullptr || buffer_ == VK_NULL_HANDLE) {
    return core::fail("nothing was captured");
  }
  device_->waitIdle();
  VmaAllocator allocator = device_->allocator();
  void* mapped = nullptr;
  if (const VkResult result = vmaMapMemory(allocator, allocation_, &mapped); result != VK_SUCCESS) {
    return core::fail(std::format("could not map the readback buffer (Vulkan error {})",
                                  static_cast<int>(result)));
  }
  const std::size_t size = std::size_t{width_} * height_ * 4;
  vmaInvalidateAllocation(allocator, allocation_, 0, VK_WHOLE_SIZE);
  CapturedImage image{.width = width_, .height = height_, .pixels = std::vector<std::byte>(size)};
  std::memcpy(image.pixels.data(), mapped, size);
  vmaUnmapMemory(allocator, allocation_);
  const bool bgra = format_ == VK_FORMAT_B8G8R8A8_UNORM || format_ == VK_FORMAT_B8G8R8A8_SRGB;
  for (std::size_t pixel = 0; pixel < size; pixel += 4) {
    if (bgra) {
      std::swap(image.pixels[pixel], image.pixels[pixel + 2]);
    }
    image.pixels[pixel + 3] = std::byte{0xFF};
  }
  release();
  return image;
}

} // namespace dwe::rhi
