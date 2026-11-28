#pragma once

#include <dwe/rhi/system_check.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::rhi {

// Why a computer cannot run the game on its graphics card, or runs it worse than it could
// (docs/architecture/graphics-diagnosis.md). gatherSystemFacts reads the facts; diagnose turns them
// into reason codes, most actionable first; diagnosisJson writes both for the launcher, which turns
// each code into a message in the player's language.

// What a GPU offers of what the renderer asks (requireRendererFeatures).
struct DeviceCaps {
  bool graphicsQueue = true;
  bool swapchain = true;
  bool samplerAnisotropy = true;
  bool descriptorIndexing = true;
  bool runtimeDescriptorArray = true;
  bool descriptorBindingPartiallyBound = true;
  bool descriptorBindingSampledImageUpdateAfterBind = true;
  bool descriptorBindingUpdateUnusedWhilePending = true;
  bool shaderSampledImageArrayNonUniformIndexing = true;
  bool bufferDeviceAddress = true;
  bool separateDepthStencilLayouts = true;
  // Core in Vulkan 1.3.
  bool dynamicRendering = true;
  bool synchronization2 = true;
  // VK_KHR_dynamic_rendering and VK_KHR_synchronization2, with their features: the compatible
  // 1.2 path.
  bool dynamicRenderingExtension = true;
  bool synchronization2Extension = true;
};

struct DeviceFacts {
  GpuInfo gpu;
  std::uint32_t deviceId = 0;
  // VkPhysicalDeviceProperties::driverVersion, encoded the vendor's way (decodeDriverVersion).
  std::uint32_t driverVersion = 0;
  // VkDriverId, 0 when the driver does not say (before Vulkan 1.2).
  std::uint32_t driverId = 0;
  std::uint64_t vramBytes = 0;
  DeviceCaps caps;
};

// A display adapter as the system lists it, with or without a Vulkan driver.
struct AdapterFacts {
  std::string name;
  std::uint32_t vendorId = 0;
  std::uint32_t deviceId = 0;
  // The kernel driver on Linux (i915, amdgpu, nvidia, nouveau...).
  std::string driver;
};

// A Vulkan layer the loader adds to every program by itself: overlays and recorders install them.
struct LayerFacts {
  std::string name;
  std::string manifest;
};

enum class StartStage : std::uint8_t { Window, Instance, Surface, SelectGpu, Device, Running };

// Where the game's own start failed (Device::create, the window, an early device lost).
struct StartFailure {
  StartStage stage = StartStage::Device;
  // A VkResult, VK_SUCCESS when the failure was not one.
  std::int32_t result = 0;
  std::string message;
};

enum class OsKind : std::uint8_t { Windows, Linux, MacOs };

struct SystemFacts {
  OsKind os = OsKind::Linux;
  bool instanceCreated = true;
  std::int32_t instanceResult = 0;
  std::uint32_t loaderVersion = 0;
  std::vector<DeviceFacts> devices;
  std::vector<AdapterFacts> adapters;
  std::vector<LayerFacts> implicitLayers;
  bool remoteSession = false;
  bool virtualMachine = false;
  bool laptop = false;
  // Linux: DISPLAY or WAYLAND_DISPLAY is set.
  bool displayAvailable = true;
  // The Vulkan driver list was narrowed (VK_DRIVER_FILES): the devices are not all the computer's.
  bool driversNarrowed = false;
  std::optional<StartFailure> start;
};

enum class Severity : std::uint8_t { Blocker, Warning, Info };

// What would fix it: a newer driver, nothing short of other hardware, a setting, or unknown.
enum class Fix : std::uint8_t { Driver, Hardware, Settings, Unknown };

struct Reason {
  std::string code;
  Severity severity = Severity::Blocker;
  Fix fix = Fix::Unknown;
  // The device the reason is about (an index into SystemFacts::devices).
  std::optional<std::size_t> device;
  std::vector<std::pair<std::string, std::string>> params;
  std::string detail;

  [[nodiscard]] std::string_view param(std::string_view key) const;
};

struct Diagnosis {
  // The device the renderer uses, when one can run the game.
  std::optional<std::size_t> chosen;
  std::vector<Reason> reasons;
};

// The floors a device passes, from its version and caps; empty lists when it passes.
[[nodiscard]] std::vector<std::string> missingForCore13(const DeviceFacts& device);
[[nodiscard]] std::vector<std::string> missingForCompatible12(const DeviceFacts& device);

// The driver's version as its maker writes it: NVIDIA 566.36, Intel on Windows 101.5186, Mesa and
// AMD major.minor.patch.
[[nodiscard]] std::string decodeDriverVersion(std::uint32_t vendorId, std::uint32_t driverId,
                                              std::uint32_t raw, OsKind os);

// The first driver of that maker with Vulkan 1.3, when it is known (gpu_diagnosis.cpp has the
// table and its sources).
[[nodiscard]] std::optional<std::string> minimumDriverFor13(std::uint32_t vendorId,
                                                            std::uint32_t driverId, OsKind os);

// -1, 0 or 1, comparing dotted numbers part by part.
[[nodiscard]] int compareVersions(std::string_view left, std::string_view right);

enum class GpuFamilySupport : std::uint8_t { Unknown, NoVulkan, UpTo12, Full };

// What the GPU's family can reach on this system with its newest driver, from its name.
[[nodiscard]] GpuFamilySupport familySupport(std::uint32_t vendorId, std::string_view name,
                                             OsKind os);

[[nodiscard]] bool knownOverlayLayer(std::string_view name);

[[nodiscard]] Diagnosis diagnose(const SystemFacts& facts);

[[nodiscard]] std::string diagnosisJson(const SystemFacts& facts, const Diagnosis& diagnosis);

[[nodiscard]] std::string_view severityName(Severity severity);
[[nodiscard]] std::string_view fixName(Fix fix);
[[nodiscard]] std::string_view osName(OsKind os);

// Reads the computer: the Vulkan devices of a fresh instance, the system's display adapters, the
// implicit layers, a remote session or a virtual machine. `start` is the game's own failure, if
// any.
[[nodiscard]] SystemFacts gatherSystemFacts(std::optional<StartFailure> start = std::nullopt);

// The last failure of Device::create in this process (each attempt overwrites it), for the
// diagnosis the game writes when it cannot start.
void noteStartFailure(StartFailure failure);
[[nodiscard]] std::optional<StartFailure> lastStartFailure();

// The line the game writes to its log for the launcher: "dwe-diagnosis 1 <json>".
inline constexpr std::string_view DiagnosisLogPrefix = "dwe-diagnosis 1 ";

} // namespace dwe::rhi
