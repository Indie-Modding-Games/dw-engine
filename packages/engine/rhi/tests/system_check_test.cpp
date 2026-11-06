#include <dwe/rhi/system_check.hpp>

#include <doctest/doctest.h>
#include <vulkan/vulkan.h>

#include <optional>
#include <string>

using namespace dwe::rhi;

TEST_CASE("the system check prints a usable GPU for the launcher") {
  const SystemCheck check{.result = SystemCheckResult::Ok,
                          .detail = "",
                          .deviceCount = 2,
                          .gpu = GpuInfo{.name = "Some GPU",
                                         .vendorId = 0x10de,
                                         .kind = GpuKind::Discrete,
                                         .apiVersion = VK_MAKE_API_VERSION(0, 1, 4, 312),
                                         .driver = "NVIDIA 580.95.05"}};
  CHECK(formatSystemCheck(check) == "dwe-check-system 1\n"
                                    "result=ok\n"
                                    "detail=\n"
                                    "devices=2\n"
                                    "gpu=Some GPU\n"
                                    "vendor=0x10de\n"
                                    "kind=discrete\n"
                                    "api=1.4.312\n"
                                    "driver=NVIDIA 580.95.05\n");
}

TEST_CASE("the system check keeps every value on its line") {
  const SystemCheck noVulkan{.result = SystemCheckResult::NoVulkan,
                             .detail = "no driver\nat all",
                             .deviceCount = 0,
                             .gpu = std::nullopt};
  CHECK(formatSystemCheck(noVulkan) == "dwe-check-system 1\nresult=no-vulkan\ndetail=no driver at "
                                       "all\ndevices=0\n");
  const SystemCheck old{.result = SystemCheckResult::Unsuitable,
                        .detail = "no suitable device",
                        .deviceCount = 1,
                        .gpu = GpuInfo{.name = "Old GPU",
                                       .vendorId = 0x8086,
                                       .kind = GpuKind::Integrated,
                                       .apiVersion = VK_MAKE_API_VERSION(0, 1, 2, 0),
                                       .driver = "0x1"}};
  const std::string text = formatSystemCheck(old);
  CHECK(text.find("result=unsuitable\n") != std::string::npos);
  CHECK(text.find("vendor=0x8086\n") != std::string::npos);
  CHECK(text.find("kind=integrated\n") != std::string::npos);
  CHECK(text.find("api=1.2.0\n") != std::string::npos);
  CHECK(formatSystemCheck({.result = SystemCheckResult::NoDevice,
                           .detail = "",
                           .deviceCount = 0,
                           .gpu = std::nullopt})
            .find("result=no-device\n") != std::string::npos);
}
