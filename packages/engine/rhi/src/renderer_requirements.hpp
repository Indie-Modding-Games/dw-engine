#pragma once

#include <VkBootstrap.h>

namespace dwe::rhi {

// The Vulkan version and the features the renderer needs from a GPU. Device::create and
// checkSystem select with the same ones, so the check says what the game will find.
//
// Without `compatibleMode` the renderer asks for Vulkan 1.3 core (dynamic rendering and
// synchronization2 are core there). With it, a Vulkan 1.2 GPU that offers those two through
// VK_KHR_dynamic_rendering and VK_KHR_synchronization2 is accepted too: this is the launcher's
// compatibility mode, for older cards and drivers whose Vulkan tops out at 1.2. Every driver that
// reports 1.3 core also still lists those two extensions, so a current GPU passes either way.
void requireRendererFeatures(vkb::PhysicalDeviceSelector& selector, bool compatibleMode = false);

// The vkGetInstanceProcAddr of the Vulkan loader the programs link, for every vk-bootstrap
// builder: without it vk-bootstrap dlopens a loader by its bare name, which on macOS never finds
// the one inside the application bundle (dyld looks only in the fallback folders). It then took
// any other Vulkan library there, and the linked loader (vkEnumeratePhysicalDevices, VMA) refused
// the instance it made ("Invalid instance"); on a Mac with none it found no Vulkan at all.
inline PFN_vkGetInstanceProcAddr vulkanEntry() {
  return &vkGetInstanceProcAddr;
}

} // namespace dwe::rhi
