#include "vulkan_calls.hpp"

namespace dwe::rhi {
namespace {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): one device per process.
Sync2Calls calls;

template <typename Function>
Function load(VkDevice device, const char* name) {
  return reinterpret_cast<Function>(vkGetDeviceProcAddr(device, name));
}

} // namespace

const Sync2Calls& sync2() {
  return calls;
}

bool useExtensionSync2(VkDevice device) {
  const Sync2Calls loaded{
      .beginRendering = load<PFN_vkCmdBeginRendering>(device, "vkCmdBeginRenderingKHR"),
      .endRendering = load<PFN_vkCmdEndRendering>(device, "vkCmdEndRenderingKHR"),
      .pipelineBarrier2 = load<PFN_vkCmdPipelineBarrier2>(device, "vkCmdPipelineBarrier2KHR"),
      .queueSubmit2 = load<PFN_vkQueueSubmit2>(device, "vkQueueSubmit2KHR")};
  if (loaded.beginRendering == nullptr || loaded.endRendering == nullptr ||
      loaded.pipelineBarrier2 == nullptr || loaded.queueSubmit2 == nullptr) {
    return false;
  }
  calls = loaded;
  return true;
}

void useCoreSync2() {
  calls = Sync2Calls{};
}

} // namespace dwe::rhi
