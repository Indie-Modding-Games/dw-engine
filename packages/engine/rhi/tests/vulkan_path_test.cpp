#include <dwe/rhi/vulkan_path.hpp>

#include <doctest/doctest.h>
#include <vulkan/vulkan.h>

using namespace dwe::rhi;

namespace {

constexpr std::uint32_t Loader14 = VK_MAKE_API_VERSION(0, 1, 4, 341);
constexpr std::uint32_t Loader13 = VK_MAKE_API_VERSION(0, 1, 3, 290);
constexpr std::uint32_t Device14 = VK_MAKE_API_VERSION(0, 1, 4, 318);
constexpr std::uint32_t Device13 = VK_MAKE_API_VERSION(0, 1, 3, 277);
constexpr std::uint32_t Device12 = VK_MAKE_API_VERSION(0, 1, 2, 199);

VulkanSupport full14() {
  return {.instanceVersion = Loader14,
          .deviceVersion = Device14,
          .hostImageCopy = true,
          .hostCopyToShaderRead = true};
}

} // namespace

TEST_CASE("versions compare by major and minor only") {
  CHECK(versionAtLeast(Device14, 1, 4));
  CHECK(versionAtLeast(VK_API_VERSION_1_4, 1, 4));
  CHECK(versionAtLeast(Device14, 1, 3));
  CHECK_FALSE(versionAtLeast(Device13, 1, 4));
  CHECK(versionAtLeast(VK_MAKE_API_VERSION(0, 2, 0, 0), 1, 4));
  CHECK_FALSE(versionAtLeast(VK_MAKE_API_VERSION(1, 1, 4, 0), 1, 4));
}

TEST_CASE("a 1.4 loader and device with host image copy take the 1.4 path") {
  CHECK(chooseVulkanPath(full14(), false, false) ==
        VulkanChoice{.path = VulkanPath::Core14, .hostImageCopy = true});
  CHECK(wantsVulkan14(Loader14, false, false));
  CHECK(apiVersionOf(VulkanPath::Core14) == VK_API_VERSION_1_4);
}

TEST_CASE("the 1.4 path copies on the host only when the driver offers it") {
  VulkanSupport support = full14();
  support.hostImageCopy = false;
  CHECK(chooseVulkanPath(support, false, false) ==
        VulkanChoice{.path = VulkanPath::Core14, .hostImageCopy = false});
  support = full14();
  support.hostCopyToShaderRead = false;
  CHECK(chooseVulkanPath(support, false, false) ==
        VulkanChoice{.path = VulkanPath::Core14, .hostImageCopy = false});
}

TEST_CASE("an older device or loader stays on the 1.3 path") {
  VulkanSupport support = full14();
  support.deviceVersion = Device13;
  CHECK(chooseVulkanPath(support, false, false) ==
        VulkanChoice{.path = VulkanPath::Core13, .hostImageCopy = false});
  support = full14();
  support.instanceVersion = Loader13;
  CHECK(chooseVulkanPath(support, false, false) ==
        VulkanChoice{.path = VulkanPath::Core13, .hostImageCopy = false});
  CHECK_FALSE(wantsVulkan14(Loader13, false, false));
  CHECK(apiVersionOf(VulkanPath::Core13) == VK_API_VERSION_1_3);
}

TEST_CASE("forcing 1.3 ignores what the driver offers") {
  CHECK(chooseVulkanPath(full14(), false, true) ==
        VulkanChoice{.path = VulkanPath::Core13, .hostImageCopy = false});
  CHECK_FALSE(wantsVulkan14(Loader14, false, true));
}

TEST_CASE("the compatible mode runs every GPU on the 1.2 path") {
  CHECK(chooseVulkanPath(full14(), true, false) ==
        VulkanChoice{.path = VulkanPath::Compatible12, .hostImageCopy = false});
  VulkanSupport old = full14();
  old.deviceVersion = Device12;
  CHECK(chooseVulkanPath(old, true, false) ==
        VulkanChoice{.path = VulkanPath::Compatible12, .hostImageCopy = false});
  CHECK_FALSE(wantsVulkan14(Loader14, true, false));
  CHECK(apiVersionOf(VulkanPath::Compatible12) == VK_API_VERSION_1_2);
  CHECK(instanceVersionFor(true, false) == VK_API_VERSION_1_2);
  CHECK(instanceVersionFor(false, false) == VK_API_VERSION_1_3);
  CHECK(instanceVersionFor(false, true) == VK_API_VERSION_1_4);
}

TEST_CASE("a failed device falls back from 1.4 to 1.3 to the compatible mode") {
  constexpr VulkanAttempt Normal{};
  constexpr VulkanAttempt Only13{.compatibleMode = false, .forceVulkan13 = true};
  constexpr VulkanAttempt Compatible{.compatibleMode = true, .forceVulkan13 = true};
  CHECK(nextVulkanAttempt(Normal, VulkanPath::Core14) == Only13);
  CHECK(nextVulkanAttempt(Only13, VulkanPath::Core13) == Compatible);
  CHECK(nextVulkanAttempt(Normal, VulkanPath::Core13) == Compatible);
  CHECK(nextVulkanAttempt(Normal, std::nullopt) == Compatible);
  CHECK(nextVulkanAttempt(Only13, std::nullopt) == Compatible);
  CHECK_FALSE(nextVulkanAttempt(Compatible, VulkanPath::Compatible12));
  CHECK_FALSE(nextVulkanAttempt(Compatible, std::nullopt));
}

TEST_CASE("a format is copied on the host only with its feature, support and fast device access") {
  CHECK(hostCopyFormatUsable(true, true, true));
  CHECK_FALSE(hostCopyFormatUsable(false, true, true));
  CHECK_FALSE(hostCopyFormatUsable(true, false, true));
  CHECK_FALSE(hostCopyFormatUsable(true, true, false));
}

TEST_CASE("each path has a name for the log") {
  CHECK(vulkanPathName(VulkanPath::Core14) == "Vulkan 1.4");
  CHECK(vulkanPathName(VulkanPath::Core13) == "Vulkan 1.3");
  CHECK(vulkanPathName(VulkanPath::Compatible12) == "Vulkan 1.2 compatible");
}
