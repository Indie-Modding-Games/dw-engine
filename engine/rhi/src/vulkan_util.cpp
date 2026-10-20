#include "vulkan_util.hpp"

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

void transitionImage(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect, VkImageLayout from,
                     VkImageLayout to, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
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
    barrier.subresourceRange = {
        .aspectMask = aspect, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};

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
                                        .levelCount = 1,
                                        .baseArrayLayer = 0,
                                        .layerCount = 1};
    vkCmdClearColorImage(commands, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &transparent, 1, &whole);
    transitionImage(commands, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

void recordImageWrite(VkCommandBuffer commands, VkImage image, VkBuffer source, VkDeviceSize offset,
                      const Rect& region) {
    // Earlier draws may still sample the image: the barrier waits for them before the copy overwrites it.
    transitionImage(commands, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_NONE,
                    VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
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
    transitionImage(commands, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
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

core::Result<void> submitAndWait(Device& device, const std::function<void(VkCommandBuffer)>& record) {
    const NativeHandles gpu = device.nativeHandles();
    OneOff oneOff;
    oneOff.device = gpu.device;

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = gpu.queueFamily;
    if (vkCreateCommandPool(gpu.device, &poolInfo, nullptr, &oneOff.pool) != VK_SUCCESS) {
        return core::fail("could not create the command pool of an upload");
    }
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = oneOff.pool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(gpu.device, &allocateInfo, &commands) != VK_SUCCESS) {
        return core::fail("could not allocate the commands of an upload");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commands, &beginInfo);
    record(commands);
    vkEndCommandBuffer(commands);

    const VkFenceCreateInfo fenceInfo{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = nullptr, .flags = 0};
    if (vkCreateFence(gpu.device, &fenceInfo, nullptr, &oneOff.fence) != VK_SUCCESS) {
        return core::fail("could not create the fence of an upload");
    }
    VkCommandBufferSubmitInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandInfo.commandBuffer = commands;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandInfo;
    if (const VkResult result = vkQueueSubmit2(gpu.queue, 1, &submit, oneOff.fence); result != VK_SUCCESS) {
        return core::fail(std::format("could not submit an upload (Vulkan error {})", static_cast<int>(result)));
    }
    if (const VkResult result = vkWaitForFences(gpu.device, 1, &oneOff.fence, VK_TRUE, UINT64_MAX);
        result != VK_SUCCESS) {
        return core::fail(std::format("an upload did not finish (Vulkan error {})", static_cast<int>(result)));
    }
    return {};
}

} // namespace dwe::rhi
