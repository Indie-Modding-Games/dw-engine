#include "vulkan_util.hpp"

#include <dwe/rhi/gpu_report.hpp>

#include <algorithm>
#include <format>

namespace dwe::rhi {

VkFormat toVulkan(Format format) {
  switch (format) {
  case Format::Undefined:
    return VK_FORMAT_UNDEFINED;
  case Format::Rgba8Unorm:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case Format::Bgra8Unorm:
    return VK_FORMAT_B8G8R8A8_UNORM;
  case Format::Depth16Unorm:
    return VK_FORMAT_D16_UNORM;
  case Format::Depth24UnormX8:
    return VK_FORMAT_X8_D24_UNORM_PACK32;
  case Format::Depth32Float:
    return VK_FORMAT_D32_SFLOAT;
  }
  return VK_FORMAT_UNDEFINED;
}

Format fromVulkan(VkFormat format) {
  switch (format) {
  case VK_FORMAT_R8G8B8A8_UNORM:
    return Format::Rgba8Unorm;
  case VK_FORMAT_B8G8R8A8_UNORM:
    return Format::Bgra8Unorm;
  case VK_FORMAT_D16_UNORM:
    return Format::Depth16Unorm;
  case VK_FORMAT_X8_D24_UNORM_PACK32:
    return Format::Depth24UnormX8;
  case VK_FORMAT_D32_SFLOAT:
    return Format::Depth32Float;
  default:
    return Format::Undefined;
  }
}

VkImageAspectFlags aspectOf(Format format) {
  return isDepthFormat(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

void transitionImage(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect,
                     VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 srcStage,
                     VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                     VkAccessFlags2 dstAccess) {
  VkImageMemoryBarrier2 barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  barrier.srcStageMask = srcStage;
  barrier.srcAccessMask = srcAccess;
  barrier.dstStageMask = dstStage;
  barrier.dstAccessMask = dstAccess;
  barrier.oldLayout = from;
  barrier.newLayout = to;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange = {.aspectMask = aspect,
                              .baseMipLevel = 0,
                              .levelCount = VK_REMAINING_MIP_LEVELS,
                              .baseArrayLayer = 0,
                              .layerCount = 1};

  VkDependencyInfo dependency{};
  dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependency.imageMemoryBarrierCount = 1;
  dependency.pImageMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(commands, &dependency);
}

void recordImageClear(VkCommandBuffer commands, VkImage image) {
  transitionImage(commands, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                  VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
  const VkClearColorValue transparent{.float32 = {0.0F, 0.0F, 0.0F, 0.0F}};
  const VkImageSubresourceRange whole{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .baseMipLevel = 0,
                                      .levelCount = VK_REMAINING_MIP_LEVELS,
                                      .baseArrayLayer = 0,
                                      .layerCount = 1};
  vkCmdClearColorImage(commands, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &transparent, 1,
                       &whole);
  transitionImage(commands, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

namespace {

// A barrier on some mip levels of a color image.
void transitionLevels(VkCommandBuffer commands, VkImage image, std::uint32_t firstLevel,
                      std::uint32_t levels, VkImageLayout from, VkImageLayout to,
                      VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                      VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
  VkImageMemoryBarrier2 barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  barrier.srcStageMask = srcStage;
  barrier.srcAccessMask = srcAccess;
  barrier.dstStageMask = dstStage;
  barrier.dstAccessMask = dstAccess;
  barrier.oldLayout = from;
  barrier.newLayout = to;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                              .baseMipLevel = firstLevel,
                              .levelCount = levels,
                              .baseArrayLayer = 0,
                              .layerCount = 1};
  VkDependencyInfo dependency{};
  dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependency.imageMemoryBarrierCount = 1;
  dependency.pImageMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(commands, &dependency);
}

// Blits every level after the first from the one before it, the image in the transfer destination
// layout, and leaves all of them in the shader read layout.
void recordMipChain(VkCommandBuffer commands, VkImage image, Extent extent,
                    std::uint32_t mipLevels) {
  const auto side = [](std::uint32_t size, std::uint32_t level) {
    return static_cast<std::int32_t>(std::max(1U, size >> level));
  };
  for (std::uint32_t level = 1; level < mipLevels; ++level) {
    transitionLevels(commands, image, level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                     VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT,
                     VK_ACCESS_2_TRANSFER_READ_BIT);
    const VkImageBlit2 blit{
        .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
        .pNext = nullptr,
        .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .mipLevel = level - 1,
                           .baseArrayLayer = 0,
                           .layerCount = 1},
        .srcOffsets = {{.x = 0, .y = 0, .z = 0},
                       {.x = side(extent.width, level - 1),
                        .y = side(extent.height, level - 1),
                        .z = 1}},
        .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .mipLevel = level,
                           .baseArrayLayer = 0,
                           .layerCount = 1},
        .dstOffsets = {{.x = 0, .y = 0, .z = 0},
                       {.x = side(extent.width, level), .y = side(extent.height, level), .z = 1}},
    };
    const VkBlitImageInfo2 info{.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
                                .pNext = nullptr,
                                .srcImage = image,
                                .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                .dstImage = image,
                                .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                .regionCount = 1,
                                .pRegions = &blit,
                                .filter = VK_FILTER_LINEAR};
    vkCmdBlitImage2(commands, &info);
  }
  transitionLevels(commands, image, 0, mipLevels - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
                   VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  transitionLevels(commands, image, mipLevels - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                   VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

} // namespace

void recordImageWrite(VkCommandBuffer commands, VkImage image, VkBuffer source, VkDeviceSize offset,
                      const Rect& region, Extent extent, std::uint32_t mipLevels) {
  // Earlier draws may still sample the image: the barrier waits for them before the copy overwrites
  // it.
  transitionImage(
      commands, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_NONE,
      VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
  const VkBufferImageCopy copy{
      .bufferOffset = offset,
      .bufferRowLength = 0,
      .bufferImageHeight = 0,
      .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .mipLevel = 0,
                           .baseArrayLayer = 0,
                           .layerCount = 1},
      .imageOffset = {.x = region.x, .y = region.y, .z = 0},
      .imageExtent = {.width = region.width, .height = region.height, .depth = 1},
  };
  vkCmdCopyBufferToImage(commands, source, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
  if (mipLevels > 1) {
    recordMipChain(commands, image, extent, mipLevels);
    return;
  }
  transitionImage(commands, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

core::Result<void> hostWriteImage(VkDevice device, const HostImageCopy& host, VkImage image,
                                  std::uint32_t mipLevels,
                                  std::span<const VkMemoryToImageCopy> regions) {
  VkHostImageLayoutTransitionInfo transition{};
  transition.sType = VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO;
  transition.image = image;
  transition.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  transition.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  transition.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .baseMipLevel = 0,
                                 .levelCount = mipLevels,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1};
  if (gpuCheck("vkTransitionImageLayout", host.transitionLayout(device, 1, &transition)) !=
      VK_SUCCESS) {
    return core::fail("could not prepare an image for a host copy");
  }
  VkCopyMemoryToImageInfo copy{};
  copy.sType = VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO;
  copy.dstImage = image;
  copy.dstImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  copy.regionCount = static_cast<std::uint32_t>(regions.size());
  copy.pRegions = regions.data();
  if (gpuCheck("vkCopyMemoryToImage", host.copyToImage(device, &copy)) != VK_SUCCESS) {
    return core::fail("could not copy texels into an image from the CPU");
  }
  return {};
}

namespace {

// The one-off command pool and fence of a submission, destroyed when it ends.
struct OneOff {
  VkDevice device = VK_NULL_HANDLE;
  VkCommandPool pool = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;

  OneOff() = default;
  OneOff(const OneOff&) = delete;
  OneOff& operator=(const OneOff&) = delete;
  OneOff(OneOff&&) = delete;
  OneOff& operator=(OneOff&&) = delete;
  ~OneOff() {
    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, pool, nullptr);
  }
};

} // namespace

core::Result<void> submitAndWait(Device& device,
                                 const std::function<void(VkCommandBuffer)>& record) {
  if (device.lost()) {
    return core::fail("the GPU device was lost");
  }
  const NativeHandles gpu = device.nativeHandles();
  OneOff oneOff;
  oneOff.device = gpu.device;

  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  poolInfo.queueFamilyIndex = gpu.queueFamily;
  if (gpuCheck("vkCreateCommandPool (upload)",
               vkCreateCommandPool(gpu.device, &poolInfo, nullptr, &oneOff.pool)) != VK_SUCCESS) {
    return core::fail("could not create the command pool of an upload");
  }
  VkCommandBufferAllocateInfo allocateInfo{};
  allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocateInfo.commandPool = oneOff.pool;
  allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocateInfo.commandBufferCount = 1;
  VkCommandBuffer commands = VK_NULL_HANDLE;
  if (gpuCheck("vkAllocateCommandBuffers (upload)",
               vkAllocateCommandBuffers(gpu.device, &allocateInfo, &commands)) != VK_SUCCESS) {
    return core::fail("could not allocate the commands of an upload");
  }

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (gpuCheck("vkBeginCommandBuffer (upload)", vkBeginCommandBuffer(commands, &beginInfo)) !=
      VK_SUCCESS) {
    return core::fail("could not begin recording an upload");
  }
  gpuNoteActivity("upload");
  record(commands);
  gpuMarker(commands, GpuWork::Upload);
  if (gpuCheck("vkEndCommandBuffer (upload)", vkEndCommandBuffer(commands)) != VK_SUCCESS) {
    return core::fail("could not finish recording an upload");
  }

  const VkFenceCreateInfo fenceInfo{
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = nullptr, .flags = 0};
  if (gpuCheck("vkCreateFence (upload)",
               vkCreateFence(gpu.device, &fenceInfo, nullptr, &oneOff.fence)) != VK_SUCCESS) {
    return core::fail("could not create the fence of an upload");
  }
  VkCommandBufferSubmitInfo commandInfo{};
  commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
  commandInfo.commandBuffer = commands;
  VkSubmitInfo2 submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
  submit.commandBufferInfoCount = 1;
  submit.pCommandBufferInfos = &commandInfo;
  if (gpuCheck("vkQueueSubmit2 (upload)", vkQueueSubmit2(gpu.queue, 1, &submit, oneOff.fence)) !=
      VK_SUCCESS) {
    return core::fail("could not submit an upload");
  }
  if (gpuCheck("vkWaitForFences (upload)",
               vkWaitForFences(gpu.device, 1, &oneOff.fence, VK_TRUE, UINT64_MAX)) != VK_SUCCESS) {
    return core::fail("an upload did not finish");
  }
  return {};
}

} // namespace dwe::rhi
