#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace dwe::rhi {

// What a check of the computer found (checkSystem).
enum class SystemCheckResult : std::uint8_t {
  // A GPU the renderer can use.
  Ok,
  // No Vulkan instance: no Vulkan driver at all, or the loader is too old.
  NoVulkan,
  // Vulkan works but lists no GPU.
  NoDevice,
  // GPUs are listed, but none has what the renderer needs (Vulkan 1.3 and its features): an old
  // GPU or an old driver.
  Unsuitable,
};

enum class GpuKind : std::uint8_t { Discrete, Integrated, Virtual, Software, Other };

struct GpuInfo {
  std::string name;
  // The PCI vendor id (0x10de, 0x1002, 0x8086...): which driver page fits.
  std::uint32_t vendorId = 0;
  GpuKind kind = GpuKind::Other;
  // VK_MAKE_API_VERSION of what the driver offers.
  std::uint32_t apiVersion = 0;
  // The driver's own name and version ("Mesa 25.2.8", "580.95.05"), or its raw number.
  std::string driver;
};

struct SystemCheck {
  SystemCheckResult result = SystemCheckResult::NoVulkan;
  // Vulkan's own words when the result is not Ok.
  std::string detail;
  std::uint32_t deviceCount = 0;
  // The GPU the renderer would pick; when none fits, the best one listed (to name its vendor).
  std::optional<GpuInfo> gpu;
  // The GPU passed only the 1.2 compatible floor: the game will run it in that mode on its own.
  bool compatible = false;
  // The diagnosis as one line of JSON (gpu_diagnosis.hpp): every GPU, the reasons, most actionable
  // first. Empty when it was not gathered.
  std::string diagnosis;
};

// Checks, without a window and without content, whether the renderer can start: creates a Vulkan
// instance and selects a GPU with the same version and features as Device::create. Quick: it makes
// no device. `dwe-game --check-system` prints it (formatSystemCheck) for the launcher.
//
// With `compatibleMode`, a Vulkan 1.2 GPU that offers dynamic rendering and synchronization2
// through their extensions counts as suitable (the launcher's compatibility mode;
// renderer_requirements.hpp). Without it, a computer that fails the 1.3 floor is checked again on
// that one, as Device::create falls back to it; `compatible` says it passed only there, and
// `detail` keeps why 1.3 failed.
SystemCheck checkSystem(bool compatibleMode = false);

class Device;

GpuInfo deviceGpu(const Device& device);

// The check as `key=value` lines under a `dwe-check-system 1` header: result (ok, no-vulkan,
// no-device, unsuitable), detail, devices, floor (1.3, or 1.2 for the compatible mode), then gpu,
// vendor (0x....), kind (discrete, integrated, virtual, software, other), api (major.minor.patch)
// and driver when a GPU was found, and diagnosis (JSON) when it was gathered. Values are on one
// line each.
std::string formatSystemCheck(const SystemCheck& check);

} // namespace dwe::rhi
