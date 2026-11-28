#pragma once

#include <vulkan/vulkan.h>

namespace dwe::rhi {

// The entry points the renderer takes from synchronization2 and dynamic rendering. On the 1.3 and
// 1.4 paths they are the loader's core ones; on the 1.2 compatible path the device has them only
// under their extension names, which a strict driver returns from vkGetDeviceProcAddr alone.
struct Sync2Calls {
  PFN_vkCmdBeginRendering beginRendering = vkCmdBeginRendering;
  PFN_vkCmdEndRendering endRendering = vkCmdEndRendering;
  PFN_vkCmdPipelineBarrier2 pipelineBarrier2 = vkCmdPipelineBarrier2;
  PFN_vkQueueSubmit2 queueSubmit2 = vkQueueSubmit2;
};

// The calls of the device that is alive (one per process).
const Sync2Calls& sync2();

// Points them at `device`'s extension entry points; false when one is missing.
bool useExtensionSync2(VkDevice device);

// Back to the core entry points (the device is gone, or a new one uses 1.3 or later).
void useCoreSync2();

} // namespace dwe::rhi
