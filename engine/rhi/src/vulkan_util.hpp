#pragma once

#include <dwe/core/error.hpp>
#include <dwe/rhi/device.hpp>
#include <dwe/rhi/types.hpp>

#include <vulkan/vulkan.h>

#include <functional>

namespace dwe::rhi {

// The Vulkan format of a Format.
VkFormat toVulkan(Format format);

// The Format of a Vulkan format, Undefined for the ones this module does not name.
Format fromVulkan(VkFormat format);

// The aspect (color or depth) of the images of a format.
VkImageAspectFlags aspectOf(Format format);

// Records a barrier that moves a whole image, of the given aspect, from one layout to another.
void transitionImage(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect, VkImageLayout from,
                     VkImageLayout to, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                     VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

// Records a clear of a whole sampled color image to transparent black, moving it from the undefined layout (its
// contents discarded) to the shader read layout.
void recordImageClear(VkCommandBuffer commands, VkImage image);

// Records a copy of tightly packed texels from source, at offset, into region of a sampled color image in the shader
// read layout, which it waits for earlier reads of and returns to.
void recordImageWrite(VkCommandBuffer commands, VkImage image, VkBuffer source, VkDeviceSize offset,
                      const Rect& region);

// Records commands with record, submits them and waits for the GPU to finish them. For uploads outside the frame:
// they come after every frame submitted so far in submission order, and before every later one.
core::Result<void> submitAndWait(Device& device, const std::function<void(VkCommandBuffer)>& record);

} // namespace dwe::rhi
