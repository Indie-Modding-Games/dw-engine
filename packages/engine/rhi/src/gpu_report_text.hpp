#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::rhi {

// What a lost device said about its fault (VK_EXT_device_fault).
std::string describeDeviceFault(const VkDeviceFaultCountsEXT& counts,
                                const VkDeviceFaultInfoEXT& fault);

// The last marker of one kind of work the GPU finished and the first it never reached, of the
// `recorded` ones (counted from 1). `label` names a marker, or nothing once it is too old.
std::string
describeMarkers(std::string_view work, std::uint32_t finished, std::uint32_t recorded,
                const std::function<std::optional<std::string_view>(std::uint32_t)>& label);

} // namespace dwe::rhi
