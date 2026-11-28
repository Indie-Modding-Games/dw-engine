#include <dwe/rhi/gpu_diagnosis.hpp>

#include <VkBootstrap.h>
#include <nlohmann/json.hpp>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "renderer_requirements.hpp"
#include "vulkan_util.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winreg.h>
#endif

namespace dwe::rhi {

namespace {

namespace fs = std::filesystem;

bool hasExtension(const std::vector<VkExtensionProperties>& extensions, std::string_view name) {
  return std::ranges::any_of(extensions, [&](const VkExtensionProperties& extension) {
    return std::string_view{static_cast<const char*>(extension.extensionName)} == name;
  });
}

DeviceFacts readDevice(VkPhysicalDevice physical, std::uint32_t instanceVersion) {
  DeviceFacts facts;
  facts.gpu = describeGpu(physical);
  // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization): Vulkan fills it in.
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(physical, &properties);
  facts.deviceId = properties.deviceID;
  facts.driverVersion = properties.driverVersion;
  const std::uint32_t api = std::min(properties.apiVersion, instanceVersion);
  const bool v11 = VK_API_VERSION_MINOR(api) >= 1 || VK_API_VERSION_MAJOR(api) > 1;
  const bool v12 = VK_API_VERSION_MINOR(api) >= 2 || VK_API_VERSION_MAJOR(api) > 1;
  const bool v13 = VK_API_VERSION_MINOR(api) >= 3 || VK_API_VERSION_MAJOR(api) > 1;

  std::uint32_t count = 0;
  vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> extensions(count);
  vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data());
  extensions.resize(count);

  if (v12) {
    // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization): Vulkan fills it in.
    VkPhysicalDeviceDriverProperties driver{};
    driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
    VkPhysicalDeviceProperties2 properties2{};
    properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties2.pNext = &driver;
    vkGetPhysicalDeviceProperties2(physical, &properties2);
    facts.driverId = static_cast<std::uint32_t>(driver.driverID);
  }

  DeviceCaps& caps = facts.caps;
  caps.swapchain = hasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  const bool dynamicExtension = hasExtension(extensions, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
  const bool sync2Extension = hasExtension(extensions, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
  if (v11) {
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceDynamicRenderingFeatures dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    VkPhysicalDeviceSynchronization2Features sync2{};
    sync2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    void** next = &features.pNext;
    const auto chain = [&next](auto& structure) {
      *next = &structure;
      next = &structure.pNext;
    };
    if (v12) {
      chain(features12);
    }
    if (v13) {
      chain(features13);
    }
    if (dynamicExtension) {
      chain(dynamic);
    }
    if (sync2Extension) {
      chain(sync2);
    }
    vkGetPhysicalDeviceFeatures2(physical, &features);
    caps.samplerAnisotropy = features.features.samplerAnisotropy == VK_TRUE;
    caps.descriptorIndexing = features12.descriptorIndexing == VK_TRUE;
    caps.runtimeDescriptorArray = features12.runtimeDescriptorArray == VK_TRUE;
    caps.descriptorBindingPartiallyBound = features12.descriptorBindingPartiallyBound == VK_TRUE;
    caps.descriptorBindingSampledImageUpdateAfterBind =
        features12.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE;
    caps.descriptorBindingUpdateUnusedWhilePending =
        features12.descriptorBindingUpdateUnusedWhilePending == VK_TRUE;
    caps.shaderSampledImageArrayNonUniformIndexing =
        features12.shaderSampledImageArrayNonUniformIndexing == VK_TRUE;
    caps.bufferDeviceAddress = features12.bufferDeviceAddress == VK_TRUE;
    caps.separateDepthStencilLayouts = features12.separateDepthStencilLayouts == VK_TRUE;
    caps.dynamicRendering = features13.dynamicRendering == VK_TRUE;
    caps.synchronization2 = features13.synchronization2 == VK_TRUE;
    caps.dynamicRenderingExtension = dynamicExtension && dynamic.dynamicRendering == VK_TRUE;
    caps.synchronization2Extension = sync2Extension && sync2.synchronization2 == VK_TRUE;
  } else {
    // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization): Vulkan fills it in.
    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(physical, &features);
    caps.samplerAnisotropy = features.samplerAnisotropy == VK_TRUE;
  }

  std::uint32_t families = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, nullptr);
  std::vector<VkQueueFamilyProperties> queues(families);
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, queues.data());
  queues.resize(families);
  caps.graphicsQueue = std::ranges::any_of(queues, [](const VkQueueFamilyProperties& queue) {
    return (queue.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
  });

  // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization): Vulkan fills it in.
  VkPhysicalDeviceMemoryProperties memory{};
  vkGetPhysicalDeviceMemoryProperties(physical, &memory);
  for (std::uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index): bounded by the count.
    const VkMemoryHeap& heap = memory.memoryHeaps[i];
    if ((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
      facts.vramBytes = std::max<std::uint64_t>(facts.vramBytes, heap.size);
    }
  }
  return facts;
}

void readVulkan(SystemFacts& facts) {
  const auto system = vkb::SystemInfo::get_system_info(vulkanEntry());
  if (!system) {
    facts.instanceCreated = false;
    facts.instanceResult = static_cast<std::int32_t>(system.vk_result());
    return;
  }
  facts.loaderVersion = system->instance_api_version;
  const std::uint32_t version = std::min(facts.loaderVersion, VK_API_VERSION_1_3);
  auto instance = vkb::InstanceBuilder{vulkanEntry()}
                      .set_app_name("dwe")
                      .set_engine_name("dwe")
                      .set_headless()
                      .require_api_version(version)
                      .build();
  if (!instance) {
    facts.instanceCreated = false;
    facts.instanceResult = static_cast<std::int32_t>(instance.vk_result());
    return;
  }
  std::uint32_t count = 0;
  vkEnumeratePhysicalDevices(instance->instance, &count, nullptr);
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(instance->instance, &count, devices.data());
  devices.resize(count);
  for (VkPhysicalDevice device : devices) {
    facts.devices.push_back(readDevice(device, version));
  }
  vkb::destroy_instance(instance.value());
}

std::string readText(const fs::path& path) {
  std::ifstream file{path, std::ios::binary};
  if (!file) {
    return {};
  }
  return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

std::vector<std::string> manifestLayers(const fs::path& manifest) {
  std::vector<std::string> names;
  const auto parsed = nlohmann::json::parse(readText(manifest), nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object()) {
    return names;
  }
  const auto add = [&names](const nlohmann::json& layer) {
    if (layer.is_object() && layer.contains("name") && layer["name"].is_string()) {
      names.push_back(layer["name"].get<std::string>());
    }
  };
  if (parsed.contains("layer")) {
    add(parsed["layer"]);
  }
  if (parsed.contains("layers") && parsed["layers"].is_array()) {
    for (const auto& layer : parsed["layers"]) {
      add(layer);
    }
  }
  return names;
}

void addManifest(SystemFacts& facts, const fs::path& manifest) {
  for (std::string& name : manifestLayers(manifest)) {
    facts.implicitLayers.push_back({.name = std::move(name), .manifest = manifest.string()});
  }
}

#if defined(_WIN32)

std::string utf8(std::wstring_view text) {
  if (text.empty()) {
    return {};
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                       nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<std::size_t>(std::max(size, 0)), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size,
                      nullptr, nullptr);
  return out;
}

std::uint32_t hexAfter(std::wstring_view text, std::wstring_view marker) {
  const auto at = text.find(marker);
  if (at == std::wstring_view::npos) {
    return 0;
  }
  std::uint32_t value = 0;
  for (wchar_t c : text.substr(at + marker.size(), 4)) {
    value <<= 4U;
    if (c >= L'0' && c <= L'9') {
      value |= static_cast<std::uint32_t>(c - L'0');
    } else if (c >= L'a' && c <= L'f') {
      value |= static_cast<std::uint32_t>(c - L'a' + 10);
    } else if (c >= L'A' && c <= L'F') {
      value |= static_cast<std::uint32_t>(c - L'A' + 10);
    }
  }
  return value;
}

void readSystem(SystemFacts& facts) {
  facts.os = OsKind::Windows;
  facts.remoteSession = GetSystemMetrics(SM_REMOTESESSION) != 0;
  SYSTEM_POWER_STATUS power{};
  facts.laptop =
      GetSystemPowerStatus(&power) != 0 && power.BatteryFlag != 128 && power.BatteryFlag != 255;
  for (DWORD index = 0;; ++index) {
    DISPLAY_DEVICEW device{};
    device.cb = sizeof(device);
    if (EnumDisplayDevicesW(nullptr, index, &device, 0) == 0) {
      break;
    }
    const std::wstring_view id{static_cast<const wchar_t*>(device.DeviceID)};
    AdapterFacts adapter{.name = utf8(static_cast<const wchar_t*>(device.DeviceString)),
                         .vendorId = hexAfter(id, L"VEN_"),
                         .deviceId = hexAfter(id, L"DEV_"),
                         .driver = {}};
    const bool known = std::ranges::any_of(facts.adapters, [&](const AdapterFacts& seen) {
      return seen.name == adapter.name && seen.deviceId == adapter.deviceId;
    });
    if (!adapter.name.empty() && !known) {
      facts.adapters.push_back(std::move(adapter));
    }
  }
  for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, L"SOFTWARE\\Khronos\\Vulkan\\ImplicitLayers", 0, KEY_READ, &key) !=
        ERROR_SUCCESS) {
      continue;
    }
    for (DWORD index = 0;; ++index) {
      std::wstring name(1024, L'\0');
      DWORD nameSize = static_cast<DWORD>(name.size());
      DWORD type = 0;
      DWORD value = 0;
      DWORD valueSize = sizeof(value);
      const LSTATUS status = RegEnumValueW(key, index, name.data(), &nameSize, nullptr, &type,
                                           reinterpret_cast<BYTE*>(&value), &valueSize);
      if (status == ERROR_NO_MORE_ITEMS) {
        break;
      }
      if (status != ERROR_SUCCESS || type != REG_DWORD || value != 0) {
        continue;
      }
      name.resize(nameSize);
      addManifest(facts, fs::path{name});
    }
    RegCloseKey(key);
  }
}

#else

void scanImplicitLayers(SystemFacts& facts) {
  std::vector<fs::path> folders;
  // NOLINTBEGIN(concurrency-mt-unsafe): read once while checking; nothing sets them.
  const char* home = std::getenv("HOME");
  const char* configHome = std::getenv("XDG_CONFIG_HOME");
  const char* dataHome = std::getenv("XDG_DATA_HOME");
  const char* configDirs = std::getenv("XDG_CONFIG_DIRS");
  const char* dataDirs = std::getenv("XDG_DATA_DIRS");
  // NOLINTEND(concurrency-mt-unsafe)
  const auto split = [&folders](const char* list, const char* fallback) {
    std::stringstream stream{list != nullptr && *list != '\0' ? list : fallback};
    std::string folder;
    while (std::getline(stream, folder, ':')) {
      if (!folder.empty()) {
        folders.push_back(fs::path{folder} / "vulkan" / "implicit_layer.d");
      }
    }
  };
  if (configHome != nullptr && *configHome != '\0') {
    folders.push_back(fs::path{configHome} / "vulkan" / "implicit_layer.d");
  } else if (home != nullptr) {
    folders.push_back(fs::path{home} / ".config" / "vulkan" / "implicit_layer.d");
  }
  split(configDirs, "/etc/xdg");
  folders.emplace_back("/etc/vulkan/implicit_layer.d");
  if (dataHome != nullptr && *dataHome != '\0') {
    folders.push_back(fs::path{dataHome} / "vulkan" / "implicit_layer.d");
  } else if (home != nullptr) {
    folders.push_back(fs::path{home} / ".local" / "share" / "vulkan" / "implicit_layer.d");
  }
  split(dataDirs, "/usr/local/share:/usr/share");
  std::error_code error;
  for (const fs::path& folder : folders) {
    for (const auto& manifest : fs::directory_iterator{folder, error}) {
      if (manifest.path().extension() == ".json") {
        addManifest(facts, manifest.path());
      }
    }
  }
}

#if defined(__APPLE__)

void readSystem(SystemFacts& facts) {
  facts.os = OsKind::MacOs;
  scanImplicitLayers(facts);
}

#else

std::string firstLine(const fs::path& path) {
  std::string text = readText(path);
  const auto end = text.find_first_of("\r\n");
  return end == std::string::npos ? text : text.substr(0, end);
}

std::string pciName(std::uint32_t vendor, std::uint32_t device) {
  for (const char* file :
       {"/usr/share/hwdata/pci.ids", "/usr/share/misc/pci.ids", "/usr/share/pci.ids"}) {
    std::ifstream ids{file};
    if (!ids) {
      continue;
    }
    const std::string vendorKey = std::format("{:04x}", vendor);
    const std::string deviceKey = std::format("\t{:04x}", device);
    std::string line;
    bool inVendor = false;
    std::string vendorName;
    while (std::getline(ids, line)) {
      if (!line.empty() && line[0] != '\t' && line[0] != '#') {
        if (inVendor) {
          break;
        }
        inVendor = line.starts_with(vendorKey);
        if (inVendor && line.size() > 6) {
          vendorName = line.substr(6);
        }
        continue;
      }
      if (inVendor && line.starts_with(deviceKey) && line.size() > 7 && line[6] != '\t') {
        return std::format("{} {}", vendorName, line.substr(7));
      }
    }
    return vendorName;
  }
  return {};
}

std::uint32_t hexFile(const fs::path& path) {
  const std::string text = firstLine(path);
  return static_cast<std::uint32_t>(std::strtoul(text.c_str(), nullptr, 16));
}

bool hasText(std::string_view text, std::string_view part) {
  return text.find(part) != std::string_view::npos;
}

void readSystem(SystemFacts& facts) {
  facts.os = OsKind::Linux;
  // NOLINTBEGIN(concurrency-mt-unsafe): read once while checking; nothing sets them.
  facts.displayAvailable =
      std::getenv("DISPLAY") != nullptr || std::getenv("WAYLAND_DISPLAY") != nullptr;
  // NOLINTEND(concurrency-mt-unsafe)
  std::error_code error;
  for (const auto& entry : fs::directory_iterator{"/sys/class/drm", error}) {
    const std::string name = entry.path().filename().string();
    if (!name.starts_with("card") || name.find('-') != std::string::npos) {
      continue;
    }
    const fs::path device = entry.path() / "device";
    AdapterFacts adapter{.name = {},
                         .vendorId = hexFile(device / "vendor"),
                         .deviceId = hexFile(device / "device"),
                         .driver = {}};
    std::error_code linkError;
    const fs::path driver = fs::read_symlink(device / "driver", linkError);
    if (!linkError) {
      adapter.driver = driver.filename().string();
    }
    adapter.name = pciName(adapter.vendorId, adapter.deviceId);
    if (adapter.vendorId != 0) {
      facts.adapters.push_back(std::move(adapter));
    }
  }
  const std::string vendor = firstLine("/sys/class/dmi/id/sys_vendor");
  const std::string product = firstLine("/sys/class/dmi/id/product_name");
  facts.virtualMachine = hasText(vendor, "QEMU") || hasText(vendor, "VMware") ||
                         hasText(vendor, "innotek") || hasText(product, "VirtualBox") ||
                         hasText(product, "KVM") || hasText(vendor, "Xen") ||
                         hasText(vendor, "Parallels") || hasText(product, "Bochs") ||
                         (hasText(vendor, "Microsoft") && hasText(product, "Virtual Machine")) ||
                         fs::exists("/sys/hypervisor/type", error);
  constexpr std::array PortableChassis{"8", "9", "10", "14", "31", "32"};
  facts.laptop =
      std::ranges::contains(PortableChassis, firstLine("/sys/class/dmi/id/chassis_type"));
  for (const auto& supply : fs::directory_iterator{"/sys/class/power_supply", error}) {
    if (firstLine(supply.path() / "type") == "Battery" &&
        firstLine(supply.path() / "scope") != "Device") {
      facts.laptop = true;
    }
  }
  scanImplicitLayers(facts);
}

#endif

#endif

} // namespace

SystemFacts gatherSystemFacts(std::optional<StartFailure> start) {
  SystemFacts facts;
  facts.start = std::move(start);
  // NOLINTBEGIN(concurrency-mt-unsafe): read once while checking; nothing sets them.
  facts.driversNarrowed =
      std::getenv("VK_DRIVER_FILES") != nullptr || std::getenv("VK_ICD_FILENAMES") != nullptr;
  // NOLINTEND(concurrency-mt-unsafe)
  readSystem(facts);
  readVulkan(facts);
  return facts;
}

} // namespace dwe::rhi
