#pragma once

#include <vulkan/vulkan.h>

namespace dwe::rhi {

// Records a barrier that moves the whole color image from one layout to another.
void transitionImage(VkCommandBuffer commands, VkImage image, VkImageLayout from, VkImageLayout to,
                     VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                     VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

} // namespace dwe::rhi
