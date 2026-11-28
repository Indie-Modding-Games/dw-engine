#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
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

// One way Device::create tries to make the device: the Vulkan floor and whether 1.4 may be used.
struct VulkanAttempt {
  bool compatibleMode = false;
  bool forceVulkan13 = false;

  bool operator==(const VulkanAttempt&) const = default;
};

// What Device::create tries after `failed` could not make a device (`reached`: the path it had
// chosen, none when no GPU was selected): 1.4, then 1.3, then the 1.2 compatible mode, then
// nothing.
[[nodiscard]] std::optional<VulkanAttempt> nextVulkanAttempt(VulkanAttempt failed,
                                                             std::optional<VulkanPath> reached);

// The version the instance asks for: 1.2 in the compatible mode, so the drivers and the validation
// layer treat every GPU as a 1.2 one and only the extensions' entry points are used.
[[nodiscard]] std::uint32_t instanceVersionFor(bool compatibleMode, bool vulkan14);

[[nodiscard]] bool hostCopyFormatUsable(bool hostTransferFeature, bool imageFormatSupported,
                                        bool optimalDeviceAccess);

[[nodiscard]] std::uint32_t apiVersionOf(VulkanPath path);

[[nodiscard]] std::string_view vulkanPathName(VulkanPath path);

} // namespace dwe::rhi
