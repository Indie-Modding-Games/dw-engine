#include <dwe/rhi/vulkan_path.hpp>

namespace dwe::rhi {

bool versionAtLeast(std::uint32_t version, std::uint32_t major, std::uint32_t minor) {
  if (VK_API_VERSION_VARIANT(version) != 0) {
    return false;
  }
  const std::uint32_t versionMajor = VK_API_VERSION_MAJOR(version);
  return versionMajor > major || (versionMajor == major && VK_API_VERSION_MINOR(version) >= minor);
}

bool wantsVulkan14(std::uint32_t loaderVersion, bool compatibleMode, bool forceVulkan13) {
  return !compatibleMode && !forceVulkan13 && versionAtLeast(loaderVersion, 1, 4);
}

VulkanChoice chooseVulkanPath(const VulkanSupport& support, bool compatibleMode,
                              bool forceVulkan13) {
  if (compatibleMode) {
    return {.path = VulkanPath::Compatible12, .hostImageCopy = false};
  }
  if (!wantsVulkan14(support.instanceVersion, compatibleMode, forceVulkan13) ||
      !versionAtLeast(support.deviceVersion, 1, 4)) {
    return {.path = VulkanPath::Core13, .hostImageCopy = false};
  }
  return {.path = VulkanPath::Core14,
          .hostImageCopy = support.hostImageCopy && support.hostCopyToShaderRead};
}

std::optional<VulkanAttempt> nextVulkanAttempt(VulkanAttempt failed,
                                               std::optional<VulkanPath> reached) {
  if (failed.compatibleMode) {
    return std::nullopt;
  }
  if (reached == VulkanPath::Core14 && !failed.forceVulkan13) {
    return VulkanAttempt{.compatibleMode = false, .forceVulkan13 = true};
  }
  return VulkanAttempt{.compatibleMode = true, .forceVulkan13 = true};
}

std::uint32_t instanceVersionFor(bool compatibleMode, bool vulkan14) {
  if (compatibleMode) {
    return VK_API_VERSION_1_2;
  }
  return vulkan14 ? VK_API_VERSION_1_4 : VK_API_VERSION_1_3;
}

bool hostCopyFormatUsable(bool hostTransferFeature, bool imageFormatSupported,
                          bool optimalDeviceAccess) {
  return hostTransferFeature && imageFormatSupported && optimalDeviceAccess;
}

std::uint32_t apiVersionOf(VulkanPath path) {
  switch (path) {
  case VulkanPath::Compatible12:
    return VK_API_VERSION_1_2;
  case VulkanPath::Core13:
    return VK_API_VERSION_1_3;
  case VulkanPath::Core14:
    return VK_API_VERSION_1_4;
  }
  return VK_API_VERSION_1_3;
}

std::string_view vulkanPathName(VulkanPath path) {
  switch (path) {
  case VulkanPath::Compatible12:
    return "Vulkan 1.2 compatible";
  case VulkanPath::Core13:
    return "Vulkan 1.3";
  case VulkanPath::Core14:
    return "Vulkan 1.4";
  }
  return "Vulkan";
}

} // namespace dwe::rhi
