#include <doctest/doctest.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "gpu_report_text.hpp"

using namespace dwe::rhi;

namespace {

std::optional<std::string_view> passName(std::uint32_t marker) {
  constexpr std::array<std::string_view, 4> Names{"shadow pass", "map pass", "models pass",
                                                  "presenting frame 9"};
  if (marker == 0 || marker > Names.size()) {
    return std::nullopt;
  }
  return Names.at(marker - 1);
}

} // namespace

TEST_CASE("a device fault lists the faulting address range and the vendor record") {
  std::array addresses{
      VkDeviceFaultAddressInfoEXT{.addressType = VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT,
                                  .reportedAddress = 0x12345678,
                                  .addressPrecision = 0x1000}};
  std::array vendor{VkDeviceFaultVendorInfoEXT{
      .description = "page fault", .vendorFaultCode = 0x2a, .vendorFaultData = 0xbeef}};
  VkDeviceFaultCountsEXT counts{};
  counts.addressInfoCount = 1;
  counts.vendorInfoCount = 1;
  VkDeviceFaultInfoEXT fault{.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT,
                             .pNext = nullptr,
                             .description = "GPU hang",
                             .pAddressInfos = addresses.data(),
                             .pVendorInfos = vendor.data(),
                             .pVendorBinaryData = nullptr};
  CHECK(describeDeviceFault(counts, fault) == "device fault: GPU hang\n"
                                              "  invalid read at 0x12345000..0x12345fff\n"
                                              "  vendor: page fault (code 0x2a, data 0xbeef)\n");
}

TEST_CASE("the markers name the last pass the GPU finished and the first it never reached") {
  CHECK(describeMarkers("frame", 2, 4, passName) ==
        "frame markers: last finished #2 map pass; first unfinished #3 models pass\n");
  CHECK(describeMarkers("frame", 0, 4, passName) ==
        "frame markers: last finished none; first unfinished #1 shadow pass\n");
  CHECK(describeMarkers("upload", 4, 4, passName) ==
        "upload markers: last finished #4 presenting frame 9; first unfinished none\n");
  CHECK(describeMarkers("frame", 4, 5, passName) ==
        "frame markers: last finished #4 presenting frame 9; first unfinished #5 (too old to "
        "name)\n");
  CHECK(describeMarkers("frame", 7, 4, passName) ==
        "frame markers: the GPU wrote #7, past the 4 recorded\n");
}
