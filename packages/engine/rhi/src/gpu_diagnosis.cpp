#include <dwe/rhi/gpu_diagnosis.hpp>

#include <nlohmann/json.hpp>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <format>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "vulkan_util.hpp"

namespace dwe::rhi {

namespace {

constexpr std::uint32_t Nvidia = 0x10de;
constexpr std::uint32_t Amd = 0x1002;
constexpr std::uint32_t AmdCpu = 0x1022;
constexpr std::uint32_t Intel = 0x8086;
constexpr std::uint64_t MiB = 1024ULL * 1024ULL;
constexpr std::uint64_t LowVram = 1024ULL * MiB;

std::string apiText(std::uint32_t version) {
  return std::format("{}.{}", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version));
}

bool atLeast(std::uint32_t version, std::uint32_t major, std::uint32_t minor) {
  const std::uint32_t got = VK_API_VERSION_MAJOR(version);
  return got > major || (got == major && VK_API_VERSION_MINOR(version) >= minor);
}

std::vector<std::string> missingCommon(const DeviceCaps& caps) {
  std::vector<std::string> missing;
  const auto need = [&missing](bool has, std::string_view name) {
    if (!has) {
      missing.emplace_back(name);
    }
  };
  need(caps.graphicsQueue, "graphicsQueue");
  need(caps.swapchain, "VK_KHR_swapchain");
  need(caps.samplerAnisotropy, "samplerAnisotropy");
  need(caps.descriptorIndexing, "descriptorIndexing");
  need(caps.runtimeDescriptorArray, "runtimeDescriptorArray");
  need(caps.descriptorBindingPartiallyBound, "descriptorBindingPartiallyBound");
  need(caps.descriptorBindingSampledImageUpdateAfterBind,
       "descriptorBindingSampledImageUpdateAfterBind");
  need(caps.descriptorBindingUpdateUnusedWhilePending, "descriptorBindingUpdateUnusedWhilePending");
  need(caps.shaderSampledImageArrayNonUniformIndexing, "shaderSampledImageArrayNonUniformIndexing");
  need(caps.bufferDeviceAddress, "bufferDeviceAddress");
  need(caps.separateDepthStencilLayouts, "separateDepthStencilLayouts");
  return missing;
}

std::string joined(const std::vector<std::string>& items) {
  std::string text;
  for (const std::string& item : items) {
    text += text.empty() ? item : ", " + item;
  }
  return text;
}

std::string lower(std::string_view text) {
  std::string out{text};
  std::ranges::transform(out, out.begin(), [](char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  });
  return out;
}

bool contains(std::string_view text, std::string_view part) {
  return lower(text).find(lower(part)) != std::string::npos;
}

bool matches(std::string_view text, const char* pattern) {
  const std::regex expression{pattern, std::regex::icase | std::regex::ECMAScript};
  return std::regex_search(text.begin(), text.end(), expression);
}

bool mesaDriver(std::uint32_t driverId) {
  switch (driverId) {
  case VK_DRIVER_ID_MESA_RADV:
  case VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA:
  case VK_DRIVER_ID_MESA_LLVMPIPE:
  case VK_DRIVER_ID_MESA_TURNIP:
  case VK_DRIVER_ID_MESA_V3DV:
  case VK_DRIVER_ID_MESA_PANVK:
  case VK_DRIVER_ID_MESA_VENUS:
  case VK_DRIVER_ID_MESA_DOZEN:
  case VK_DRIVER_ID_MESA_NVK:
  case VK_DRIVER_ID_IMAGINATION_OPEN_SOURCE_MESA:
  case VK_DRIVER_ID_MESA_HONEYKRISP:
    return true;
  default:
    return false;
  }
}

std::string_view vendorName(std::uint32_t vendorId) {
  switch (vendorId) {
  case Nvidia:
    return "nvidia";
  case Amd:
  case AmdCpu:
    return "amd";
  case Intel:
    return "intel";
  default:
    return "other";
  }
}

std::string_view familyName(GpuFamilySupport support) {
  switch (support) {
  case GpuFamilySupport::NoVulkan:
    return "no-vulkan";
  case GpuFamilySupport::UpTo12:
    return "up-to-1.2";
  case GpuFamilySupport::Full:
    return "full";
  case GpuFamilySupport::Unknown:
    return "unknown";
  }
  return "unknown";
}

std::string resultName(std::int32_t result) {
  switch (result) {
  case VK_SUCCESS:
    return "VK_SUCCESS";
  case VK_ERROR_OUT_OF_HOST_MEMORY:
    return "VK_ERROR_OUT_OF_HOST_MEMORY";
  case VK_ERROR_OUT_OF_DEVICE_MEMORY:
    return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
  case VK_ERROR_INITIALIZATION_FAILED:
    return "VK_ERROR_INITIALIZATION_FAILED";
  case VK_ERROR_DEVICE_LOST:
    return "VK_ERROR_DEVICE_LOST";
  case VK_ERROR_LAYER_NOT_PRESENT:
    return "VK_ERROR_LAYER_NOT_PRESENT";
  case VK_ERROR_EXTENSION_NOT_PRESENT:
    return "VK_ERROR_EXTENSION_NOT_PRESENT";
  case VK_ERROR_FEATURE_NOT_PRESENT:
    return "VK_ERROR_FEATURE_NOT_PRESENT";
  case VK_ERROR_INCOMPATIBLE_DRIVER:
    return "VK_ERROR_INCOMPATIBLE_DRIVER";
  case VK_ERROR_SURFACE_LOST_KHR:
    return "VK_ERROR_SURFACE_LOST_KHR";
  case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
    return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
  default:
    return std::format("VkResult {}", result);
  }
}

bool basicAdapter(const AdapterFacts& adapter) {
  return contains(adapter.name, "Microsoft Basic Display") ||
         contains(adapter.name, "Microsoft Basic Render");
}

bool remoteAdapter(const AdapterFacts& adapter) {
  return contains(adapter.name, "Remote Display Adapter") ||
         contains(adapter.name, "Remote Desktop");
}

bool virtualAdapter(const AdapterFacts& adapter) {
  constexpr std::array Names{"VMware",    "VirtualBox", "Hyper-V", "QXL",   "virtio",
                             "Parallels", "Citrix",     "Bochs",   "Cirrus"};
  constexpr std::array Drivers{"vmwgfx",    "virtio_gpu", "virtio-gpu", "qxl",    "bochs",
                               "bochs-drm", "vboxvideo",  "hyperv_drm", "cirrus", "cirrus-qemu"};
  return std::ranges::any_of(Names,
                             [&](const char* name) { return contains(adapter.name, name); }) ||
         std::ranges::any_of(Drivers, [&](const char* driver) { return adapter.driver == driver; });
}

bool realVendor(std::uint32_t vendorId) {
  return vendorId == Nvidia || vendorId == Amd || vendorId == AmdCpu || vendorId == Intel;
}

enum class Floor : std::uint8_t { None, Compatible12, Core13 };

Floor floorOf(const DeviceFacts& device) {
  if (missingForCore13(device).empty()) {
    return Floor::Core13;
  }
  if (missingForCompatible12(device).empty()) {
    return Floor::Compatible12;
  }
  return Floor::None;
}

int kindRank(GpuKind kind) {
  switch (kind) {
  case GpuKind::Discrete:
    return 0;
  case GpuKind::Integrated:
    return 1;
  case GpuKind::Virtual:
    return 2;
  case GpuKind::Other:
    return 3;
  case GpuKind::Software:
    return 4;
  }
  return 4;
}

int reasonRank(const Reason& reason) {
  constexpr std::array<std::string_view, 20> Order{"window-failed",
                                                   "remote-session",
                                                   "implicit-layers",
                                                   "basic-display-adapter",
                                                   "dedicated-gpu-unused",
                                                   "driver-too-old",
                                                   "no-vendor-driver",
                                                   "out-of-memory",
                                                   "device-lost",
                                                   "vulkan-too-old",
                                                   "missing-features",
                                                   "instance-failed",
                                                   "device-failed",
                                                   "virtual-machine",
                                                   "compatible-path",
                                                   "hardware-no-vulkan",
                                                   "no-gpu",
                                                   "low-vram",
                                                   "integrated-gpu",
                                                   ""};
  const auto found = std::ranges::find(Order, reason.code);
  int rank = static_cast<int>(found - Order.begin());
  if ((reason.code == "vulkan-too-old" || reason.code == "missing-features") &&
      reason.fix != Fix::Driver) {
    rank += 6;
  }
  return rank;
}

class Builder {
public:
  explicit Builder(const SystemFacts& facts) : facts_(facts) {}

  Reason& add(std::string_view code, Severity severity, Fix fix) {
    reasons_.push_back({.code = std::string{code},
                        .severity = severity,
                        .fix = fix,
                        .device = std::nullopt,
                        .params = {},
                        .detail = {}});
    return reasons_.back();
  }

  Reason& about(Reason& reason, std::size_t index) const {
    const DeviceFacts& device = facts_.devices[index];
    reason.device = index;
    reason.params.emplace_back("gpu", device.gpu.name);
    reason.params.emplace_back("vendor", vendorName(device.gpu.vendorId));
    reason.params.emplace_back("driver", decodeDriverVersion(device.gpu.vendorId, device.driverId,
                                                             device.driverVersion, facts_.os));
    reason.params.emplace_back("api", apiText(device.gpu.apiVersion));
    return reason;
  }

  std::vector<Reason> take() { return std::move(reasons_); }
  [[nodiscard]] bool has(std::string_view code) const {
    return std::ranges::any_of(reasons_, [&](const Reason& r) { return r.code == code; });
  }

private:
  const SystemFacts& facts_;
  std::vector<Reason> reasons_;
};

std::optional<std::string> tooOldDriver(const DeviceFacts& device, OsKind os) {
  const auto minimum = minimumDriverFor13(device.gpu.vendorId, device.driverId, os);
  if (!minimum) {
    return std::nullopt;
  }
  const std::string installed =
      decodeDriverVersion(device.gpu.vendorId, device.driverId, device.driverVersion, os);
  if (compareVersions(installed, *minimum) >= 0) {
    return std::nullopt;
  }
  return minimum;
}

// What fixes a device that misses a floor: a driver when its family reaches the floor with a newer
// one, the hardware when the family stops short.
Fix fixFor(const DeviceFacts& device, OsKind os) {
  const GpuFamilySupport family = familySupport(device.gpu.vendorId, device.gpu.name, os);
  switch (family) {
  case GpuFamilySupport::NoVulkan:
    return Fix::Hardware;
  case GpuFamilySupport::UpTo12:
    return atLeast(device.gpu.apiVersion, 1, 2) ? Fix::Hardware : Fix::Driver;
  case GpuFamilySupport::Full:
    return tooOldDriver(device, os) || !atLeast(device.gpu.apiVersion, 1, 3) ? Fix::Driver
                                                                             : Fix::Unknown;
  case GpuFamilySupport::Unknown:
    return tooOldDriver(device, os) ? Fix::Driver : Fix::Unknown;
  }
  return Fix::Unknown;
}

// The 1.3 driver row applies only to a family that reaches 1.3.
std::optional<std::string> driverRow(const DeviceFacts& device, OsKind os) {
  const GpuFamilySupport family = familySupport(device.gpu.vendorId, device.gpu.name, os);
  if (family != GpuFamilySupport::Full && family != GpuFamilySupport::Unknown) {
    return std::nullopt;
  }
  return tooOldDriver(device, os);
}

void explainFailingDevice(Builder& builder, const SystemFacts& facts, std::size_t index,
                          Severity severity) {
  const DeviceFacts& device = facts.devices[index];
  const Fix fix = fixFor(device, facts.os);
  const GpuFamilySupport family = familySupport(device.gpu.vendorId, device.gpu.name, facts.os);
  if (const auto minimum = driverRow(device, facts.os)) {
    Reason& reason = builder.about(builder.add("driver-too-old", severity, Fix::Driver), index);
    reason.params.emplace_back("minimum", *minimum);
  }
  if (!atLeast(device.gpu.apiVersion, 1, 2)) {
    Reason& reason = builder.about(builder.add("vulkan-too-old", severity, fix), index);
    reason.params.emplace_back("required", "1.2");
    reason.params.emplace_back("family", familyName(family));
    return;
  }
  const std::vector<std::string> missing = missingForCompatible12(device);
  if (!missing.empty()) {
    Reason& reason = builder.about(builder.add("missing-features", severity, fix), index);
    reason.params.emplace_back("features", joined(missing));
    reason.params.emplace_back("family", familyName(family));
  }
}

std::optional<std::size_t> chooseDevice(const SystemFacts& facts) {
  std::optional<std::size_t> chosen;
  for (std::size_t i = 0; i < facts.devices.size(); ++i) {
    const DeviceFacts& device = facts.devices[i];
    if (floorOf(device) == Floor::None) {
      continue;
    }
    if (!chosen) {
      chosen = i;
      continue;
    }
    const DeviceFacts& best = facts.devices[*chosen];
    const auto key = [](const DeviceFacts& d) {
      return std::pair{kindRank(d.gpu.kind), floorOf(d) == Floor::Core13 ? 0 : 1};
    };
    if (key(device) < key(best)) {
      chosen = i;
    }
  }
  return chosen;
}

// A dedicated GPU the system lists that Vulkan does not, while an integrated one draws.
const AdapterFacts* unusedDedicated(const SystemFacts& facts, std::uint32_t chosenVendor) {
  for (const AdapterFacts& adapter : facts.adapters) {
    const bool dedicatedVendor =
        adapter.vendorId == Nvidia || (adapter.vendorId == Amd && chosenVendor != Amd);
    if (!dedicatedVendor || adapter.vendorId == chosenVendor) {
      continue;
    }
    const bool inVulkan = std::ranges::any_of(facts.devices, [&](const DeviceFacts& d) {
      return d.gpu.vendorId == adapter.vendorId &&
             (adapter.deviceId == 0 || d.deviceId == adapter.deviceId);
    });
    if (!inVulkan) {
      return &adapter;
    }
  }
  return nullptr;
}

std::vector<std::string> overlayLayers(const SystemFacts& facts) {
  std::vector<std::string> names;
  for (const LayerFacts& layer : facts.implicitLayers) {
    if (knownOverlayLayer(layer.name) && std::ranges::find(names, layer.name) == names.end()) {
      names.push_back(layer.name);
    }
  }
  return names;
}

void explainNoVulkanGpu(Builder& builder, const SystemFacts& facts) {
  if (facts.driversNarrowed) {
    return;
  }
  if (const auto basic = std::ranges::find_if(facts.adapters, basicAdapter);
      basic != facts.adapters.end()) {
    Reason& reason = builder.add("basic-display-adapter", Severity::Blocker, Fix::Driver);
    reason.params.emplace_back("adapter", basic->name);
    reason.params.emplace_back("vendor", vendorName(basic->vendorId));
    return;
  }
  if (facts.virtualMachine || std::ranges::any_of(facts.adapters, virtualAdapter)) {
    builder.add("virtual-machine", Severity::Blocker, Fix::Settings);
    return;
  }
  bool named = false;
  for (const AdapterFacts& adapter : facts.adapters) {
    if (!realVendor(adapter.vendorId) || remoteAdapter(adapter)) {
      continue;
    }
    named = true;
    const GpuFamilySupport family = familySupport(adapter.vendorId, adapter.name, facts.os);
    const bool hardware = family == GpuFamilySupport::NoVulkan;
    Reason& reason = builder.add(hardware ? "hardware-no-vulkan" : "no-vendor-driver",
                                 Severity::Blocker, hardware ? Fix::Hardware : Fix::Driver);
    reason.params.emplace_back(
        "adapter", adapter.name.empty()
                       ? std::format("{:#06x}:{:#06x}", adapter.vendorId, adapter.deviceId)
                       : adapter.name);
    reason.params.emplace_back("vendor", vendorName(adapter.vendorId));
    if (!adapter.driver.empty()) {
      reason.params.emplace_back("kernel", adapter.driver);
    }
  }
  if (!named && !facts.remoteSession) {
    builder.add("no-gpu", Severity::Blocker, Fix::Unknown);
  }
}

void explainStart(Builder& builder, const SystemFacts& facts, std::optional<std::size_t> chosen) {
  if (!facts.start || facts.start->stage == StartStage::Window) {
    return;
  }
  const StartFailure& start = *facts.start;
  const auto withDevice = [&](Reason& reason) -> Reason& {
    if (chosen) {
      builder.about(reason, *chosen);
    }
    reason.params.emplace_back("result", resultName(start.result));
    reason.detail = start.message;
    return reason;
  };
  if (start.result == VK_ERROR_OUT_OF_DEVICE_MEMORY ||
      start.result == VK_ERROR_OUT_OF_HOST_MEMORY) {
    Reason& reason = withDevice(builder.add("out-of-memory", Severity::Blocker, Fix::Settings));
    if (chosen) {
      reason.params.emplace_back("vram", std::to_string(facts.devices[*chosen].vramBytes / MiB));
    }
  } else if (start.result == VK_ERROR_DEVICE_LOST || start.stage == StartStage::Running) {
    withDevice(builder.add("device-lost", Severity::Blocker, Fix::Driver));
  } else if (start.stage == StartStage::Surface) {
    withDevice(builder.add("window-failed", Severity::Blocker, Fix::Settings));
  } else if (start.stage == StartStage::Instance && !builder.has("instance-failed")) {
    withDevice(builder.add("instance-failed", Severity::Blocker, Fix::Driver));
  } else if (start.stage == StartStage::Device) {
    withDevice(builder.add("device-failed", Severity::Blocker, Fix::Driver));
  }
}

std::optional<StartFailure>& startFailure() {
  static std::optional<StartFailure> failure;
  return failure;
}

} // namespace

void noteStartFailure(StartFailure failure) {
  startFailure() = std::move(failure);
}

std::optional<StartFailure> lastStartFailure() {
  return startFailure();
}

std::string_view Reason::param(std::string_view key) const {
  const auto found = std::ranges::find(params, key, &std::pair<std::string, std::string>::first);
  return found == params.end() ? std::string_view{} : std::string_view{found->second};
}

std::vector<std::string> missingForCore13(const DeviceFacts& device) {
  if (!atLeast(device.gpu.apiVersion, 1, 3)) {
    return {"Vulkan 1.3"};
  }
  std::vector<std::string> missing = missingCommon(device.caps);
  if (!device.caps.dynamicRendering) {
    missing.emplace_back("dynamicRendering");
  }
  if (!device.caps.synchronization2) {
    missing.emplace_back("synchronization2");
  }
  return missing;
}

std::vector<std::string> missingForCompatible12(const DeviceFacts& device) {
  if (!atLeast(device.gpu.apiVersion, 1, 2)) {
    return {"Vulkan 1.2"};
  }
  std::vector<std::string> missing = missingCommon(device.caps);
  if (!device.caps.dynamicRenderingExtension) {
    missing.emplace_back("VK_KHR_dynamic_rendering");
  }
  if (!device.caps.synchronization2Extension) {
    missing.emplace_back("VK_KHR_synchronization2");
  }
  return missing;
}

std::string decodeDriverVersion(std::uint32_t vendorId, std::uint32_t driverId, std::uint32_t raw,
                                OsKind os) {
  const bool nvidia =
      driverId == VK_DRIVER_ID_NVIDIA_PROPRIETARY || (driverId == 0 && vendorId == Nvidia);
  if (nvidia) {
    const std::uint32_t major = (raw >> 22U) & 0x3ffU;
    const std::uint32_t minor = (raw >> 14U) & 0xffU;
    const std::uint32_t patch = (raw >> 6U) & 0xffU;
    return patch == 0 ? std::format("{}.{:02}", major, minor)
                      : std::format("{}.{:02}.{:02}", major, minor, patch);
  }
  const bool intelWindows = driverId == VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS ||
                            (driverId == 0 && vendorId == Intel && os == OsKind::Windows);
  if (intelWindows) {
    return std::format("{}.{}", raw >> 14U, raw & 0x3fffU);
  }
  return std::format("{}.{}.{}", VK_API_VERSION_MAJOR(raw) | (VK_API_VERSION_VARIANT(raw) << 7U),
                     VK_API_VERSION_MINOR(raw), VK_API_VERSION_PATCH(raw));
}

std::optional<std::string> minimumDriverFor13(std::uint32_t vendorId, std::uint32_t driverId,
                                              OsKind os) {
  // First drivers with Vulkan 1.3, from the makers' notes (checked 2026-11-27):
  //  - NVIDIA: 511.65 on Windows, 510.47.03 on Linux (release notes, production branch,
  //    2022-02-01); developer.nvidia.com/vulkan-driver had 473.11 and 470.62.22 betas before.
  //  - Intel on Windows: 30.0.101.1660 (geeks3d, 2022-04-01; Intel's own notes were not reachable).
  //  - Mesa (RADV, ANV): 22.0.0 (docs.mesa3d.org/relnotes/22.0.0.html, 2022-03-09).
  //  - AMD's own driver reports its Vulkan driver version (2.0.x), not the Adrenalin one, so it has
  //    no row: its devices are judged by the Vulkan version they report.
  if (driverId == VK_DRIVER_ID_MESA_RADV || driverId == VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA) {
    return "22.0.0";
  }
  if (mesaDriver(driverId)) {
    return std::nullopt;
  }
  if (driverId == VK_DRIVER_ID_NVIDIA_PROPRIETARY || (driverId == 0 && vendorId == Nvidia)) {
    if (os == OsKind::Windows) {
      return "511.65";
    }
    if (os == OsKind::Linux) {
      return "510.47.03";
    }
    return std::nullopt;
  }
  if (driverId == VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS ||
      (driverId == 0 && vendorId == Intel && os == OsKind::Windows)) {
    return "101.1660";
  }
  return std::nullopt;
}

int compareVersions(std::string_view left, std::string_view right) {
  const auto parts = [](std::string_view text) {
    std::vector<unsigned> numbers;
    const char* at = text.data();
    const char* end = text.data() + text.size();
    while (at < end) {
      unsigned value = 0;
      const auto [next, error] = std::from_chars(at, end, value);
      numbers.push_back(error == std::errc{} ? value : 0U);
      at = std::find(next, end, '.');
      if (at != end) {
        ++at;
      }
    }
    return numbers;
  };
  std::vector<unsigned> a = parts(left);
  std::vector<unsigned> b = parts(right);
  const std::size_t size = std::max(a.size(), b.size());
  a.resize(size, 0);
  b.resize(size, 0);
  const auto order = a <=> b;
  return order < 0 ? -1 : (order > 0 ? 1 : 0);
}

GpuFamilySupport familySupport(std::uint32_t vendorId, std::string_view name, OsKind os) {
  // From the makers' support notes (checked 2026-11-27): NVIDIA Fermi and older and AMD TeraScale
  // never had Vulkan; NVIDIA Kepler's last drivers (474.89 on Windows) stop at Vulkan 1.2; Intel
  // has no Windows Vulkan driver before the 6th generation (Skylake); on Linux Mesa's hasvk gives
  // Ivy Bridge and Haswell Vulkan 1.2 and Broadwell 1.3. AMD GCN 1 to 3 on Windows end with
  // the 22.6.1 legacy driver, whose Vulkan version is not published: unknown.
  if (vendorId == Nvidia) {
    if (matches(
            name,
            R"(GeForce\s+(8|9)\d{3}|G?T[SX]?\s*[1-3]\d{2}\b|GTX?\s*[45]\d{2}\b|Quadro\s+(FX|NVS))")) {
      return GpuFamilySupport::NoVulkan;
    }
    if (matches(name, R"(GTX\s*6\d{2}\b|GTX\s*7[6-8]0|TITAN(\s+Black|\s+Z)?\s*$)")) {
      return GpuFamilySupport::UpTo12;
    }
    return GpuFamilySupport::Full;
  }
  if (vendorId == Intel) {
    if (matches(name, R"(HD Graphics\s*(2000|3000)\b)")) {
      return GpuFamilySupport::NoVulkan;
    }
    const bool gen7 = matches(
        name,
        R"(HD Graphics\s*(2500|4000|4200|4400|4600|5000)\b|Iris(\(TM\))?\s+(Pro\s+)?Graphics\s*(5100|5200)\b)");
    const bool gen8 = matches(
        name,
        R"(HD Graphics\s*(5300|5500|5600|6000)\b|Iris(\(TM\))?\s+(Pro\s+)?Graphics\s*(6100|6200)\b)");
    const bool atom = matches(name, R"(HD Graphics\s*$)");
    if (os == OsKind::Windows && (gen7 || gen8 || atom)) {
      return GpuFamilySupport::NoVulkan;
    }
    if (gen7) {
      return GpuFamilySupport::UpTo12;
    }
    return GpuFamilySupport::Full;
  }
  if (vendorId == Amd || vendorId == AmdCpu) {
    if (matches(name, R"(Radeon\s+HD\s*[2-6]\d{3}\b)")) {
      return GpuFamilySupport::NoVulkan;
    }
    if (os == OsKind::Windows &&
        matches(name, R"(Radeon\s+HD\s*7\d{3}\b|R[79]\s*[23]\d{2}\b|R9\s+(Fury|Nano))")) {
      return GpuFamilySupport::Unknown;
    }
    return GpuFamilySupport::Full;
  }
  return GpuFamilySupport::Unknown;
}

bool knownOverlayLayer(std::string_view name) {
  // Layers that ship with overlays, recorders and injectors and are known to break a game's
  // start (checked 2026-11-27 against their manifests and engines' workarounds). Drivers' own
  // helper layers (NVIDIA Optimus, Mesa device select) are left out.
  constexpr std::array Known{"VK_LAYER_OBS_HOOK",
                             "VK_LAYER_reshade",
                             "VK_LAYER_MANGOHUD_overlay",
                             "VK_LAYER_RTSS",
                             "VK_LAYER_bandicam_helper",
                             "VK_LAYER_OW_OVERLAY",
                             "VK_LAYER_OW_OBS_HOOK",
                             "VK_LAYER_EOS_Overlay",
                             "VK_LAYER_VALVE_steam_overlay",
                             "VK_LAYER_AMD_switchable_graphics",
                             "VK_LAYER_GAMEPP",
                             "VK_LAYER_TENCENT_wegame_cross_overlay"};
  return std::ranges::any_of(
      Known, [&](std::string_view known) { return lower(name).starts_with(lower(known)); });
}

Diagnosis diagnose(const SystemFacts& facts) {
  Builder builder{facts};
  Diagnosis diagnosis;
  if (facts.start && facts.start->stage == StartStage::Window) {
    Reason& reason = builder.add("window-failed", Severity::Blocker, Fix::Settings);
    reason.detail = facts.start->message;
    if (facts.os == OsKind::Linux && !facts.displayAvailable) {
      reason.params.emplace_back("display", "none");
    }
  }
  if (facts.remoteSession || std::ranges::any_of(facts.adapters, remoteAdapter)) {
    builder.add("remote-session", Severity::Blocker, Fix::Settings);
  }
  const bool startFailed = facts.start && facts.start->stage != StartStage::Window;
  if (const std::vector<std::string> overlays = overlayLayers(facts);
      !overlays.empty() && (!facts.instanceCreated || startFailed)) {
    builder.add("implicit-layers", Severity::Blocker, Fix::Settings)
        .params.emplace_back("layers", joined(overlays));
  }
  const bool noDriverAtAll =
      !facts.instanceCreated && facts.instanceResult == VK_ERROR_INCOMPATIBLE_DRIVER;
  if (!facts.instanceCreated && !noDriverAtAll) {
    builder.add("instance-failed", Severity::Blocker, Fix::Driver)
        .params.emplace_back("result", resultName(facts.instanceResult));
  }
  diagnosis.chosen = chooseDevice(facts);
  const bool anyReal = std::ranges::any_of(
      facts.devices, [](const DeviceFacts& d) { return d.gpu.kind != GpuKind::Software; });
  if (!diagnosis.chosen || facts.devices[*diagnosis.chosen].gpu.kind == GpuKind::Software) {
    const bool softwareOnly = diagnosis.chosen.has_value();
    if (!anyReal && (facts.instanceCreated || noDriverAtAll)) {
      explainNoVulkanGpu(builder, facts);
    }
    for (std::size_t i = 0; i < facts.devices.size(); ++i) {
      if (facts.devices[i].gpu.kind != GpuKind::Software) {
        explainFailingDevice(builder, facts, i, Severity::Blocker);
      }
    }
    if (const AdapterFacts* dedicated = unusedDedicated(facts, 0); dedicated && anyReal) {
      Reason& reason = builder.add("dedicated-gpu-unused", Severity::Blocker, Fix::Driver);
      reason.params.emplace_back("dedicated", dedicated->name);
      reason.params.emplace_back("dedicatedVendor", vendorName(dedicated->vendorId));
    }
    if (softwareOnly && facts.driversNarrowed) {
      diagnosis.chosen.reset();
    }
  } else {
    const std::size_t chosen = *diagnosis.chosen;
    const DeviceFacts& device = facts.devices[chosen];
    if (floorOf(device) == Floor::Compatible12) {
      const Fix fix = fixFor(device, facts.os);
      Reason& reason =
          builder.about(builder.add("compatible-path", Severity::Warning, fix), chosen);
      reason.params.emplace_back("features", joined(missingForCore13(device)));
      if (const auto minimum = driverRow(device, facts.os)) {
        builder.about(builder.add("driver-too-old", Severity::Warning, Fix::Driver), chosen)
            .params.emplace_back("minimum", *minimum);
      }
    }
    if (device.gpu.kind == GpuKind::Integrated) {
      if (const AdapterFacts* dedicated = unusedDedicated(facts, device.gpu.vendorId)) {
        Reason& reason = builder.about(
            builder.add("dedicated-gpu-unused", Severity::Warning, Fix::Driver), chosen);
        reason.params.emplace_back("dedicated", dedicated->name);
        reason.params.emplace_back("dedicatedVendor", vendorName(dedicated->vendorId));
      }
      for (std::size_t i = 0; i < facts.devices.size(); ++i) {
        if (facts.devices[i].gpu.kind == GpuKind::Discrete &&
            floorOf(facts.devices[i]) == Floor::None) {
          explainFailingDevice(builder, facts, i, Severity::Warning);
        }
      }
      builder.about(builder.add("integrated-gpu", Severity::Info, Fix::Hardware), chosen);
    }
    if (device.gpu.kind == GpuKind::Virtual) {
      builder.about(builder.add("virtual-machine", Severity::Warning, Fix::Settings), chosen);
    }
    if (device.gpu.kind == GpuKind::Discrete && device.vramBytes > 0 &&
        device.vramBytes < LowVram) {
      builder.about(builder.add("low-vram", Severity::Warning, Fix::Hardware), chosen)
          .params.emplace_back("vram", std::to_string(device.vramBytes / MiB));
    }
  }
  explainStart(builder, facts, diagnosis.chosen);
  diagnosis.reasons = builder.take();
  std::ranges::stable_sort(diagnosis.reasons, [](const Reason& a, const Reason& b) {
    return std::pair{a.severity, reasonRank(a)} < std::pair{b.severity, reasonRank(b)};
  });
  return diagnosis;
}

std::string_view severityName(Severity severity) {
  switch (severity) {
  case Severity::Blocker:
    return "blocker";
  case Severity::Warning:
    return "warning";
  case Severity::Info:
    return "info";
  }
  return "info";
}

std::string_view fixName(Fix fix) {
  switch (fix) {
  case Fix::Driver:
    return "driver";
  case Fix::Hardware:
    return "hardware";
  case Fix::Settings:
    return "settings";
  case Fix::Unknown:
    return "unknown";
  }
  return "unknown";
}

std::string_view osName(OsKind os) {
  switch (os) {
  case OsKind::Windows:
    return "windows";
  case OsKind::Linux:
    return "linux";
  case OsKind::MacOs:
    return "macos";
  }
  return "linux";
}

std::string diagnosisJson(const SystemFacts& facts, const Diagnosis& diagnosis) {
  using nlohmann::json;
  json gpus = json::array();
  for (const DeviceFacts& device : facts.devices) {
    const std::vector<std::string> missing13 = missingForCore13(device);
    const std::vector<std::string> missing12 = missingForCompatible12(device);
    gpus.push_back({
        {"name", device.gpu.name},
        {"vendor", std::format("{:#06x}", device.gpu.vendorId)},
        {"device", std::format("{:#06x}", device.deviceId)},
        {"kind", gpuKindName(device.gpu.kind)},
        {"api", std::format("{}.{}.{}", VK_API_VERSION_MAJOR(device.gpu.apiVersion),
                            VK_API_VERSION_MINOR(device.gpu.apiVersion),
                            VK_API_VERSION_PATCH(device.gpu.apiVersion))},
        {"driver", device.gpu.driver},
        {"driverVersion",
         decodeDriverVersion(device.gpu.vendorId, device.driverId, device.driverVersion, facts.os)},
        {"vramMiB", device.vramBytes / MiB},
        {"floor", missing13.empty() ? "1.3" : (missing12.empty() ? "1.2" : "none")},
        {"missing", missing12.empty() ? missing13 : missing12},
    });
  }
  json adapters = json::array();
  for (const AdapterFacts& adapter : facts.adapters) {
    adapters.push_back({{"name", adapter.name},
                        {"vendor", std::format("{:#06x}", adapter.vendorId)},
                        {"device", std::format("{:#06x}", adapter.deviceId)},
                        {"driver", adapter.driver}});
  }
  json layers = json::array();
  for (const LayerFacts& layer : facts.implicitLayers) {
    layers.push_back({{"name", layer.name}, {"manifest", layer.manifest}});
  }
  json reasons = json::array();
  for (const Reason& reason : diagnosis.reasons) {
    json params = json::object();
    for (const auto& [key, value] : reason.params) {
      params[key] = value;
    }
    json item = {{"code", reason.code},
                 {"severity", severityName(reason.severity)},
                 {"fix", fixName(reason.fix)},
                 {"params", params},
                 {"detail", reason.detail}};
    if (reason.device) {
      item["gpu"] = *reason.device;
    }
    reasons.push_back(std::move(item));
  }
  json root = {
      {"version", 1},
      {"os", osName(facts.os)},
      {"loader",
       {{"version", apiText(facts.loaderVersion)},
        {"instance", facts.instanceCreated ? "VK_SUCCESS" : resultName(facts.instanceResult)}}},
      {"remote", facts.remoteSession},
      {"virtual", facts.virtualMachine},
      {"laptop", facts.laptop},
      {"narrowed", facts.driversNarrowed},
      {"gpus", gpus},
      {"adapters", adapters},
      {"layers", layers},
      {"reasons", reasons},
  };
  root["chosen"] = diagnosis.chosen ? json(*diagnosis.chosen) : json(nullptr);
  if (facts.start) {
    root["start"] = {{"result", resultName(facts.start->result)},
                     {"message", facts.start->message}};
  }
  return root.dump(-1, ' ', false, json::error_handler_t::replace);
}

} // namespace dwe::rhi
