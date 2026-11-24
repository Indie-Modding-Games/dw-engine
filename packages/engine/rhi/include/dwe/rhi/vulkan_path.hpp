#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string_view>

namespace dwe::rhi {

enum class VulkanPath : std::uint8_t { Compatible12, Core13, Core14 };

struct VulkanSupport {
  std::uint32_t instanceVersion = 0;
  std::uint32_t deviceVersion = 0;
  bool hostImageCopy = false;
  bool hostCopyToShaderRead = false;
};

struct VulkanChoice {
  VulkanPath path = VulkanPath::Core13;
  bool hostImageCopy = false;

  bool operator==(const VulkanChoice&) const = default;
};

[[nodiscard]] bool versionAtLeast(std::uint32_t version, std::uint32_t major, std::uint32_t minor);

[[nodiscard]] bool wantsVulkan14(std::uint32_t loaderVersion, bool compatibleMode,
                                 bool forceVulkan13);

[[nodiscard]] VulkanChoice chooseVulkanPath(const VulkanSupport& support, bool compatibleMode,
                                            bool forceVulkan13);

[[nodiscard]] bool hostCopyFormatUsable(bool hostTransferFeature, bool imageFormatSupported,
                                        bool optimalDeviceAccess);

[[nodiscard]] std::uint32_t apiVersionOf(VulkanPath path);

[[nodiscard]] std::string_view vulkanPathName(VulkanPath path);

} // namespace dwe::rhi
