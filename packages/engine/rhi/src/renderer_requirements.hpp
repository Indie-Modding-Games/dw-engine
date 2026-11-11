#pragma once

#include <VkBootstrap.h>

namespace dwe::rhi {

// The Vulkan version and the features the renderer needs from a GPU. Device::create and
// checkSystem select with the same ones, so the check says what the game will find.
void requireRendererFeatures(vkb::PhysicalDeviceSelector& selector);

// The vkGetInstanceProcAddr of the Vulkan loader the programs link, for every vk-bootstrap
// builder: without it vk-bootstrap dlopens a loader by its bare name, which on macOS never finds
// the one inside the application bundle (dyld looks only in the fallback folders). It then took
// any other Vulkan library there, and the linked loader (vkEnumeratePhysicalDevices, VMA) refused
// the instance it made ("Invalid instance"); on a Mac with none it found no Vulkan at all.
inline PFN_vkGetInstanceProcAddr vulkanEntry() {
  return &vkGetInstanceProcAddr;
}

} // namespace dwe::rhi
