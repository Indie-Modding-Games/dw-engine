#pragma once

#include <VkBootstrap.h>

namespace dwe::rhi {

// The Vulkan version and the features the renderer needs from a GPU. Device::create and
// checkSystem select with the same ones, so the check says what the game will find.
void requireRendererFeatures(vkb::PhysicalDeviceSelector& selector);

} // namespace dwe::rhi
