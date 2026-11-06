#include <dwe/rhi/buffer.hpp>

#include <vk_mem_alloc.h>

#include <cstring>
#include <format>
#include <utility>

#include "vulkan_util.hpp"

namespace dwe::rhi {
namespace {

VkBufferUsageFlags toVulkan(BufferUsage usage) {
  VkBufferUsageFlags flags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  if (hasAll(usage, BufferUsage::Vertex)) {
    flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  }
  if (hasAll(usage, BufferUsage::Index)) {
    flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  }
  if (hasAll(usage, BufferUsage::Uniform)) {
    flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  }
  if (hasAll(usage, BufferUsage::Storage)) {
    flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  }
  return flags;
}

} // namespace

core::Result<Buffer> Buffer::create(Device& device, const BufferDesc& desc) {
  if (desc.size == 0) {
    return core::fail("a buffer needs at least one byte");
  }
  VkBufferUsageFlags usage = toVulkan(desc.usage);
  // Device buffers are filled by copies; host buffers may be the source of one (staging).
  usage |= desc.location == MemoryLocation::Device ? VK_BUFFER_USAGE_TRANSFER_DST_BIT
                                                   : VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = desc.size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo allocationInfo{};
  allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
  if (desc.location == MemoryLocation::Host) {
    allocationInfo.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    // Coherent, so writes need no flush before the frame that reads them is submitted.
    allocationInfo.requiredFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  }

  Buffer buffer{device};
  VmaAllocationInfo allocated{};
  if (const VkResult result = vmaCreateBuffer(device.allocator(), &bufferInfo, &allocationInfo,
                                              &buffer.buffer_, &buffer.allocation_, &allocated);
      result != VK_SUCCESS) {
    buffer.device_ = nullptr;
    return core::fail(std::format("could not create a buffer of {} bytes (Vulkan error {})",
                                  desc.size, static_cast<int>(result)));
  }
  buffer.size_ = desc.size;
  if (desc.location == MemoryLocation::Host) {
    buffer.mapped_ = {static_cast<std::byte*>(allocated.pMappedData), desc.size};
  }
  const VkBufferDeviceAddressInfo addressInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
                                              .pNext = nullptr,
                                              .buffer = buffer.buffer_};
  buffer.address_ = vkGetBufferDeviceAddress(device.nativeHandles().device, &addressInfo);
  return buffer;
}

core::Result<Buffer> Buffer::createMapped(Device& device, BufferUsage usage,
                                          std::span<const std::byte> data) {
  auto buffer =
      create(device, {.size = data.size(), .usage = usage, .location = MemoryLocation::Host});
  if (buffer && !data.empty()) {
    std::memcpy(buffer->mapped().data(), data.data(), data.size());
  }
  return buffer;
}

core::Result<Buffer> Buffer::createWithData(Device& device, BufferUsage usage,
                                            std::span<const std::byte> data) {
  auto buffer =
      create(device, {.size = data.size(), .usage = usage, .location = MemoryLocation::Device});
  if (!buffer) {
    return buffer;
  }
  auto staging =
      create(device, {.size = data.size(), .usage = usage, .location = MemoryLocation::Host});
  if (!staging) {
    return std::unexpected{staging.error()};
  }
  std::memcpy(staging->mapped().data(), data.data(), data.size());
  auto copied = submitAndWait(device, [&](VkCommandBuffer commands) {
    const VkBufferCopy region{.srcOffset = 0, .dstOffset = 0, .size = data.size()};
    vkCmdCopyBuffer(commands, staging->native(), buffer->native(), 1, &region);
    // Later submissions come after this barrier in submission order, so every frame that draws sees
    // the data.
    VkMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commands, &dependency);
  });
  if (!copied) {
    return std::unexpected{copied.error()};
  }
  return buffer;
}

Buffer::Buffer(Buffer&& other) noexcept
    : device_(std::exchange(other.device_, nullptr)),
      buffer_(std::exchange(other.buffer_, VK_NULL_HANDLE)),
      allocation_(std::exchange(other.allocation_, VK_NULL_HANDLE)),
      size_(std::exchange(other.size_, 0)), address_(std::exchange(other.address_, 0)),
      mapped_(std::exchange(other.mapped_, {})) {}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
  if (this != &other) {
    release();
    device_ = std::exchange(other.device_, nullptr);
    buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
    allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
    size_ = std::exchange(other.size_, 0);
    address_ = std::exchange(other.address_, 0);
    mapped_ = std::exchange(other.mapped_, {});
  }
  return *this;
}

Buffer::~Buffer() {
  release();
}

void Buffer::release() {
  if (device_ == nullptr) {
    return;
  }
  device_->destroyLater(
      [allocator = device_->allocator(), buffer = buffer_, allocation = allocation_] {
        vmaDestroyBuffer(allocator, buffer, allocation);
      });
  device_ = nullptr;
}

} // namespace dwe::rhi
