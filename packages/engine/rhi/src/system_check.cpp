#include <dwe/rhi/system_check.hpp>

#include <VkBootstrap.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <iterator>
#include <string_view>
#include <vector>

#include "renderer_requirements.hpp"

namespace dwe::rhi {

void requireRendererFeatures(vkb::PhysicalDeviceSelector& selector) {
  VkPhysicalDeviceVulkan13Features features13{};
  features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  features13.dynamicRendering = VK_TRUE;
  features13.synchronization2 = VK_TRUE;

  // Descriptor indexing for the bindless texture table and buffer device addresses for per-frame
  // data (docs/architecture/render.md).
  VkPhysicalDeviceVulkan12Features features12{};
  features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  features12.descriptorIndexing = VK_TRUE;
  features12.runtimeDescriptorArray = VK_TRUE;
  features12.descriptorBindingPartiallyBound = VK_TRUE;
  features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
  features12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
  features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
  features12.bufferDeviceAddress = VK_TRUE;
  // Depth-only attachment layouts for the depth target.
  features12.separateDepthStencilLayouts = VK_TRUE;
  VkPhysicalDeviceFeatures features{};
  features.samplerAnisotropy = VK_TRUE;

  // The selector keeps copies of the feature structures.
  selector.set_minimum_version(1, 3)
      .set_required_features(features)
      .set_required_features_12(features12)
      .set_required_features_13(features13);
}

namespace {

GpuKind kindOf(VkPhysicalDeviceType type) {
  switch (type) {
  case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
    return GpuKind::Discrete;
  case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
    return GpuKind::Integrated;
  case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
    return GpuKind::Virtual;
  case VK_PHYSICAL_DEVICE_TYPE_CPU:
    return GpuKind::Software;
  default:
    return GpuKind::Other;
  }
}

// A Vulkan fixed-size text field, up to its terminating zero.
template <std::size_t Size>
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays): Vulkan's fields are C arrays.
std::string_view fixedText(const char (&text)[Size]) {
  return {std::begin(text), std::ranges::find(text, '\0')};
}

GpuInfo describe(VkPhysicalDevice device) {
  // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization): Vulkan fills it in.
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(device, &properties);
  GpuInfo info{.name = std::string{fixedText(properties.deviceName)},
               .vendorId = properties.vendorID,
               .kind = kindOf(properties.deviceType),
               .apiVersion = properties.apiVersion,
               .driver = std::format("{:#x}", properties.driverVersion)};
  // The driver's own name and version are core from Vulkan 1.2 on.
  if (VK_API_VERSION_MINOR(properties.apiVersion) >= 2 ||
      VK_API_VERSION_MAJOR(properties.apiVersion) > 1) {
    // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization): Vulkan fills it in.
    VkPhysicalDeviceDriverProperties driver{};
    driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
    VkPhysicalDeviceProperties2 properties2{};
    properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties2.pNext = &driver;
    vkGetPhysicalDeviceProperties2(device, &properties2);
    const std::string_view name = fixedText(driver.driverName);
    const std::string_view version = fixedText(driver.driverInfo);
    if (name.empty() || version.empty()) {
      info.driver = name.empty() ? std::string{version} : std::string{name};
    } else {
      info.driver = std::format("{} {}", name, version);
    }
    if (info.driver.empty()) {
      info.driver = std::format("{:#x}", properties.driverVersion);
    }
  }
  return info;
}

// The listed GPU whose vendor the player should look at when none fits: a real GPU before a
// virtual or a software one.
std::optional<GpuInfo> bestListed(VkInstance instance) {
  std::uint32_t count = 0;
  if (vkEnumeratePhysicalDevices(instance, &count, nullptr) != VK_SUCCESS || count == 0) {
    return std::nullopt;
  }
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(instance, &count, devices.data());
  devices.resize(count);
  std::vector<GpuInfo> listed;
  listed.reserve(devices.size());
  for (VkPhysicalDevice device : devices) {
    listed.push_back(describe(device));
  }
  constexpr std::array Order{GpuKind::Discrete, GpuKind::Integrated, GpuKind::Virtual,
                             GpuKind::Other, GpuKind::Software};
  const auto rank = [&](const GpuInfo& gpu) {
    return std::ranges::find(Order, gpu.kind) - Order.begin();
  };
  return *std::ranges::min_element(listed, {}, rank);
}

std::uint32_t countDevices(VkInstance instance) {
  std::uint32_t count = 0;
  return vkEnumeratePhysicalDevices(instance, &count, nullptr) == VK_SUCCESS ? count : 0;
}

std::string_view resultName(SystemCheckResult result) {
  switch (result) {
  case SystemCheckResult::Ok:
    return "ok";
  case SystemCheckResult::NoVulkan:
    return "no-vulkan";
  case SystemCheckResult::NoDevice:
    return "no-device";
  case SystemCheckResult::Unsuitable:
    return "unsuitable";
  }
  return "no-vulkan";
}

std::string_view kindName(GpuKind kind) {
  switch (kind) {
  case GpuKind::Discrete:
    return "discrete";
  case GpuKind::Integrated:
    return "integrated";
  case GpuKind::Virtual:
    return "virtual";
  case GpuKind::Software:
    return "software";
  case GpuKind::Other:
    return "other";
  }
  return "other";
}

// One line: line breaks of a driver's or Vulkan's text become spaces.
std::string oneLine(std::string_view text) {
  std::string line{text};
  std::ranges::replace(line, '\n', ' ');
  std::ranges::replace(line, '\r', ' ');
  return line;
}

} // namespace

SystemCheck checkSystem() {
  SystemCheck check;
  // Not headless: the builder asks for the window-system extensions a window will need. The
  // linked loader's vkGetInstanceProcAddr (vulkanEntry), as for the game's own instance.
  auto instance = vkb::InstanceBuilder{vulkanEntry()}
                      .set_app_name("dwe")
                      .set_engine_name("dwe")
                      .require_api_version(1, 3, 0)
                      .build();
  if (!instance) {
    check.result = SystemCheckResult::NoVulkan;
    check.detail = instance.error().message();
    return check;
  }
  check.deviceCount = countDevices(instance->instance);
  if (check.deviceCount == 0) {
    check.result = SystemCheckResult::NoDevice;
    check.detail = "Vulkan lists no GPU";
    vkb::destroy_instance(instance.value());
    return check;
  }
  vkb::PhysicalDeviceSelector selector{instance.value()};
  requireRendererFeatures(selector);
  // No window: the surface is checked when the game opens one.
  auto selected = selector.defer_surface_initialization().select();
  if (selected) {
    check.result = SystemCheckResult::Ok;
    check.gpu = describe(selected->physical_device);
  } else {
    check.result = SystemCheckResult::Unsuitable;
    check.detail = selected.error().message();
    check.gpu = bestListed(instance->instance);
  }
  vkb::destroy_instance(instance.value());
  return check;
}

std::string formatSystemCheck(const SystemCheck& check) {
  std::string text = "dwe-check-system 1\n";
  text += std::format("result={}\ndetail={}\ndevices={}\n", resultName(check.result),
                      oneLine(check.detail), check.deviceCount);
  if (check.gpu) {
    const GpuInfo& gpu = *check.gpu;
    text += std::format("gpu={}\nvendor={:#06x}\nkind={}\napi={}.{}.{}\ndriver={}\n",
                        oneLine(gpu.name), gpu.vendorId, kindName(gpu.kind),
                        VK_API_VERSION_MAJOR(gpu.apiVersion), VK_API_VERSION_MINOR(gpu.apiVersion),
                        VK_API_VERSION_PATCH(gpu.apiVersion), oneLine(gpu.driver));
  }
  return text;
}

} // namespace dwe::rhi
