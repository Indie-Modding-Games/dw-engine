#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/rhi/gpu_report.hpp>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <format>
#include <mutex>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#endif

namespace dwe::rhi {
namespace {

// How many calls the trail keeps. Enough to show the pipeline build, the upload and the submit that
// led to a hang or a fault inside a driver.
constexpr std::size_t TrailLength = 12;

// A PCI vendor id, as a name: which driver page to look at.
std::string_view vendorName(std::uint32_t vendorId) {
  switch (vendorId) {
  case 0x10deU:
    return "NVIDIA";
  case 0x1002U:
    return "AMD";
  case 0x8086U:
    return "Intel";
  case 0x15adU:
    return "VMware";
  case 0x1af5U:
    return "Broadcom";
  case 0x13e0U:
    return "Xilinx";
  case 0x19eeU:
    return "Qualcomm";
  case 0x5143U:
    return "Qualcomm (Arm)";
  case 0x1010U:
    return "imgtec";
  case 0x10c3U:
    return "Arm";
  case 0x1d1fU:
    return "3DFX";
  case 0x1028U:
    return "Huawei";
  case 0x1ae0U:
    return "Apple";
  default:
    return "an unknown vendor";
  }
}

// Vulkan's driver id (VkDriverId), named without the VK_DRIVER_ID_* macros so the report stays
// stable across loader versions.
std::string_view driverIdName(std::uint32_t driverId) {
  switch (driverId) {
  case 0U:
    return "unknown";
  case 1U:
    return "AMD proprietary";
  case 2U:
    return "AMD open-source (AMDVLK)";
  case 3U:
    return "Mesa RADV";
  case 4U:
    return "NVIDIA proprietary";
  case 5U:
    return "Intel proprietary (Windows)";
  case 6U:
    return "Intel one driver (Linux)";
  case 7U:
    return "Mesa ANV";
  case 8U:
    return "Qualcomm";
  case 9U:
    return "Arm";
  case 10U:
    return "PowerVR";
  case 11U:
    return "imagination";
  case 12U:
    return "Mesa V3D";
  case 13U:
    return "Broadcom";
  case 14U:
    return "Tegra";
  case 15U:
    return "Breadboard (Google SwiftShader?)";
  case 16U:
    return "Broadcom VideoCore";
  case 17U:
    return "Mesa Etnaviv";
  case 18U:
    return "Mesa Freedreno";
  case 19U:
    return "Mesa Turnip";
  case 20U:
    return "Mesa Panvk";
  case 21U:
    return "Mesa Dozen";
  case 22U:
    return "Mesa Asahi";
  case 23U:
    return "Mesa Etnaviv";
  case 24U:
    return "Mesa Freedreno";
  case 25U:
    return "Mesa Turnip";
  case 26U:
    return "Mesa Panvk";
  case 27U:
    return "Mesa Dozen";
  case 28U:
    return "Mesa Asahi";
  default:
    return "an unnamed driver";
  }
}

// Vulkan's own name for a result, for the few that a crash usually comes back with; the rest fall
// back to their number.
std::string_view resultName(VkResult result) {
  switch (result) {
  case VK_SUCCESS:
    return "VK_SUCCESS";
  case VK_NOT_READY:
    return "VK_NOT_READY";
  case VK_TIMEOUT:
    return "VK_TIMEOUT";
  case VK_EVENT_SET:
    return "VK_EVENT_SET";
  case VK_EVENT_RESET:
    return "VK_EVENT_RESET";
  case VK_INCOMPLETE:
    return "VK_INCOMPLETE";
  case VK_ERROR_OUT_OF_HOST_MEMORY:
    return "VK_ERROR_OUT_OF_HOST_MEMORY";
  case VK_ERROR_OUT_OF_DEVICE_MEMORY:
    return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
  case VK_ERROR_INITIALIZATION_FAILED:
    return "VK_ERROR_INITIALIZATION_FAILED";
  case VK_ERROR_DEVICE_LOST:
    return "VK_ERROR_DEVICE_LOST";
  case VK_ERROR_MEMORY_MAP_FAILED:
    return "VK_ERROR_MEMORY_MAP_FAILED";
  case VK_ERROR_LAYER_NOT_PRESENT:
    return "VK_ERROR_LAYER_NOT_PRESENT";
  case VK_ERROR_EXTENSION_NOT_PRESENT:
    return "VK_ERROR_EXTENSION_NOT_PRESENT";
  case VK_ERROR_FEATURE_NOT_PRESENT:
    return "VK_ERROR_FEATURE_NOT_PRESENT";
  case VK_ERROR_TOO_MANY_OBJECTS:
    return "VK_ERROR_TOO_MANY_OBJECTS";
  case VK_ERROR_FORMAT_NOT_SUPPORTED:
    return "VK_ERROR_FORMAT_NOT_SUPPORTED";
  case VK_ERROR_UNKNOWN:
    return "VK_ERROR_UNKNOWN";
  case VK_ERROR_FRAGMENTATION:
    return "VK_ERROR_FRAGMENTATION";
  case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
    return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
  case VK_ERROR_OUT_OF_DATE_KHR:
    return "VK_ERROR_OUT_OF_DATE_KHR";
  case VK_SUBOPTIMAL_KHR:
    return "VK_SUBOPTIMAL_KHR";
  default:
    return "VkResult";
  }
}

// Whether the result is an error rather than one of the success codes or the benign swapchain
// states (out of date and suboptimal) that the device already handles by rebuilding the swapchain.
bool isFailure(VkResult result) {
  switch (result) {
  case VK_SUCCESS:
  case VK_NOT_READY:
  case VK_TIMEOUT:
  case VK_EVENT_SET:
  case VK_EVENT_RESET:
  case VK_INCOMPLETE:
  case VK_SUBOPTIMAL_KHR:
  case VK_ERROR_OUT_OF_DATE_KHR:
    return false;
  default:
    return true;
  }
}

// The process-wide record. The trail is read by a crash handler with no Device, so it lives here,
// behind one mutex; a plain string, and a fixed buffer for the parts a signal handler may write.
struct Record {
  std::mutex mutex;
  std::string gpu;
  std::string vendor;
  std::string driver;
  std::string limits;
  std::string activity;
  std::deque<std::string> trail;
  std::uint32_t vendorId = 0;
  bool deviceLost = false;
  bool installed = false;
  bool reportBuilt = false;
  // How many calls have entered the trail; the next slot to write is writes % TrailLength, and the
  // oldest entry is the one after the newest once the ring has turned.
  std::size_t writes = 0;
  // The fields a signal handler writes from (fixed buffers, no allocation).
  std::array<char, 256> gpuText{};
  std::array<char, 256> driverText{};
  std::array<char, 256> activityText{};
  std::array<std::array<char, 96>, TrailLength> callText{};

  // Copies a string into a fixed buffer for the signal handler to print.
  void snapshot(std::array<char, 256>& buffer, const std::string& text) {
    const std::size_t taken = std::min(text.size(), buffer.size() - 1);
    std::memcpy(buffer.data(), text.data(), taken);
    buffer[taken] = '\0';
  }

  // The most recent calls, oldest first, with the newest last.
  void noteCall(std::string_view call, VkResult result) {
    // The slot to overwrite is the one after the newest yet, taken from a running counter: the
    // trail's last index is fixed once it is full and would keep overwriting one slot.
    const std::size_t slot = writes++ % TrailLength;
    trail.push_back(std::format("{}={} ({})", call, static_cast<int>(result), resultName(result)));
    while (trail.size() > TrailLength) {
      trail.pop_front();
    }
    std::snprintf(callText[slot].data(), callText[slot].size(), "%s", trail.back().c_str());
  }
};

Record& record() {
  // A leak on purpose: the crash handlers run while the process is unwinding and the record must
  // still be alive then (a destroyed mutex or freed string would crash the handler too).
  static Record* instance = new Record();
  return *instance;
}

std::string formatVersion(std::uint32_t version) {
  return std::format("{}.{}.{}", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version),
                     VK_API_VERSION_PATCH(version));
}

void appendReport(std::string& text, const Record& rec) {
  text += "dwe graphics crash report\n";
  if (!rec.gpu.empty()) {
    text += std::format("gpu:     {}\n", rec.gpu);
  }
  if (!rec.driver.empty()) {
    text += std::format("driver:  {}\n", rec.driver);
  }
  if (!rec.limits.empty()) {
    text += std::format("limits:  {}\n", rec.limits);
  }
  if (!rec.activity.empty()) {
    text += std::format("activity: {}\n", rec.activity);
  }
  text += "recent Vulkan calls:\n";
  if (rec.trail.empty()) {
    text += "  (none recorded)\n";
  }
  for (const std::string& call : rec.trail) {
    text += "  " + call + "\n";
  }
}

// Writes the report through the plain-C buffers, for the crash handlers: no allocation, no mutex,
// so it is safe to run while the process dies inside a driver.
void writeReportToStderr(const Record& rec) {
  std::fputs("=== dwe graphics crash report ===\n", stderr);
  std::fprintf(stderr, "gpu:      %s\n", rec.gpuText.data());
  std::fprintf(stderr, "driver:   %s\n", rec.driverText.data());
  std::fprintf(stderr, "activity: %s\n", rec.activityText.data());
  std::fputs("recent Vulkan calls (newest last):\n", stderr);
  // The ring turns once full: the oldest entry is the slot the next write will take, so print from
  // there to keep the calls in time order even across the wrap.
  const std::size_t start = rec.writes < TrailLength ? 0 : rec.writes % TrailLength;
  for (std::size_t step = 0; step < TrailLength; ++step) {
    const std::array<char, 96>& call = rec.callText[(start + step) % TrailLength];
    if (call[0] != '\0') {
      std::fprintf(stderr, "  %s\n", call.data());
    }
  }
  std::fflush(stderr);
}

} // namespace

void gpuReportDevice(const NativeHandles& handles, bool validation) {
  // One chain of property structures: the driver's own properties, the 1.2 ones, and the descriptor
  // indexing limits (a GPU that clamps them low is a GPU where bindless goes wrong).
  VkPhysicalDeviceDriverProperties driver{};
  driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
  VkPhysicalDeviceDescriptorIndexingProperties indexing{};
  indexing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;
  VkPhysicalDeviceVulkan12Properties properties12{};
  properties12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
  properties12.pNext = &driver;
  driver.pNext = &indexing;
  indexing.pNext = nullptr;
  VkPhysicalDeviceProperties2 properties{};
  properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  properties.pNext = &properties12;
  vkGetPhysicalDeviceProperties2(handles.physicalDevice, &properties);

  const VkPhysicalDeviceProperties& base = properties.properties;
  const std::uint32_t driverId = driver.driverID;

  // The driver: its VkDriverId as a name (which separates, say, Mesa ANV from the Intel proprietary
  // Windows driver), then whatever human-readable name and version the driver itself gives in
  // VkPhysicalDeviceDriverProperties::driverName and driverInfo.
  std::string driverText{driverIdName(driverId)};
  const bool driverHasName = driver.driverName[0] != '\0';
  const bool driverHasInfo = driver.driverInfo[0] != '\0';
  if (driverHasName && driverHasInfo) {
    driverText = std::format("{}: {} {}", driverText, driver.driverName, driver.driverInfo);
  } else if (driverHasName || driverHasInfo) {
    driverText =
        std::format("{}: {}", driverText, driverHasName ? driver.driverName : driver.driverInfo);
  } else {
    driverText = std::format("{} (driver version {:#x})", driverText, base.driverVersion);
  }

  // Whether the GPU samples BC7: an old or software GPU (and some iGPUs) does not, and then the
  // renderer transcodes to RGBA8 (the path that fails on Intel drivers if it does).
  bool bc7 = true;
  for (const VkFormat format : {VK_FORMAT_BC7_UNORM_BLOCK, VK_FORMAT_BC7_SRGB_BLOCK}) {
    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(handles.physicalDevice, format, &formatProperties);
    if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0) {
      bc7 = false;
    }
  }

  const std::string gpu =
      std::format("{} (vendor 0x{:04x} {}, type {:d})", base.deviceName, base.vendorID,
                  vendorName(base.vendorID), static_cast<int>(base.deviceType));
  const std::string limits = std::format(
      "api {}, max image {}x{}, max anisotropy {}, BC7 {}, bindless images up to {}",
      formatVersion(base.apiVersion), base.limits.maxImageDimension2D,
      base.limits.maxImageDimension2D, base.limits.maxSamplerAnisotropy, bc7 ? "yes" : "NO",
      std::min(indexing.maxUpdateAfterBindDescriptorsInAllPools,
               std::min(properties12.maxDescriptorSetUpdateAfterBindSampledImages,
                        properties12.maxPerStageDescriptorUpdateAfterBindSampledImages)));

  Record& rec = record();
  {
    const std::lock_guard lock(rec.mutex);
    rec.gpu = gpu;
    rec.vendor = std::string{vendorName(base.vendorID)};
    rec.vendorId = base.vendorID;
    rec.driver = std::format("{}; api {}", driverText, formatVersion(base.apiVersion));
    rec.limits = std::format("{}; validation {}", limits, validation ? "on" : "off");
    rec.reportBuilt = true;
    rec.snapshot(rec.gpuText, gpu);
    rec.snapshot(rec.driverText, rec.driver);
    rec.snapshot(rec.activityText, rec.activity);
  }
  core::logInfo("gpu report: {}", gpu);
  core::logInfo("gpu report: driver {}", rec.driver);
  core::logInfo("gpu report: {}", rec.limits);
}

void gpuNoteActivity(std::string_view activity) {
  Record& rec = record();
  const std::lock_guard lock(rec.mutex);
  if (rec.activity == activity) {
    return;
  }
  rec.activity = activity;
  const std::size_t taken = std::min(activity.size(), rec.activityText.size() - 1);
  std::memcpy(rec.activityText.data(), activity.data(), taken);
  rec.activityText[taken] = '\0';
}

std::string gpuActivity() {
  const Record& rec = record();
  const std::lock_guard lock(rec.mutex);
  return rec.activity;
}

VkResult gpuCheck(std::string_view call, VkResult result) {
  // The benign swapchain results are not recorded: they happen on every window resize and would
  // push the calls that matter out of the trail.
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    return result;
  }
  Record& rec = record();
  {
    const std::lock_guard lock(rec.mutex);
    // Every call the renderer marks is recorded, success or not: a fault that kills the process
    // inside a driver never returns a failure to log, so the trail of the calls that did return is
    // what names the one in flight.
    rec.noteCall(call, result);
    if (result == VK_ERROR_DEVICE_LOST) {
      rec.deviceLost = true;
    }
  }
  if (!isFailure(result)) {
    return result;
  }
  const std::string text = gpuFailureReport();
  if (result == VK_ERROR_DEVICE_LOST) {
    core::logError(
        "vulkan: {} reported VK_ERROR_DEVICE_LOST: the GPU reset or hung and the device cannot be "
        "used again; drawing stops.\n{}",
        call, text);
  } else {
    core::logError("vulkan: {} failed with {} ({}).\n{}", call, resultName(result),
                   static_cast<int>(result), text);
  }
  return result;
}

bool gpuDeviceLost() {
  const Record& rec = record();
  const std::lock_guard lock(rec.mutex);
  return rec.deviceLost;
}

std::string gpuFailureReport() {
  Record& rec = record();
  std::string text;
  {
    const std::lock_guard lock(rec.mutex);
    appendReport(text, rec);
  }
  return text;
}

std::uint64_t gpuSpirvHash(std::span<const std::uint32_t> spirv) {
  // FNV-1a over the SPIR-V words.
  std::uint64_t hash = 14695981039346656037ULL;
  for (const std::uint32_t word : spirv) {
    for (std::size_t byte = 0; byte < sizeof(word); ++byte) {
      hash ^= (word >> (byte * 8)) & 0xFFU;
      hash *= 1099511628211ULL;
    }
  }
  return hash;
}

namespace {

// The previous terminate handler, kept so the normal crash path still runs after the report.
std::terminate_handler previousTerminate = nullptr;

// Runs when an exception reaches the top without being caught (a bad_alloc in a pipeline build, a
// throw from a library). The mutex is NOT taken: another thread might hold it and the process is
// already dying, so we only read the fixed C buffers, which the normal writer updates lock-free.
void crashTerminate() {
  static std::atomic<bool> entered{false};
  if (!entered.exchange(true)) {
    writeReportToStderr(record());
  }
  if (previousTerminate != nullptr) {
    previousTerminate();
  }
  std::abort();
}

#ifdef _WIN32

LONG WINAPI nativeCrashHandler(PEXCEPTION_POINTERS pointers) {
  const DWORD code = pointers->ExceptionRecord->ExceptionCode;
  // Anything the driver raises with severity 11 (the top two bits of the NTSTATUS code set): the
  // access violation of the AMD proprietary compiler crash, the fast-fail from abort (which is what
  // DWE_ASSERT reaches on Windows), stack overflow, divide by zero, illegal instruction. Breakpoint
  // (0x80000003), single-step, and the MSVC C++ exception (0xE06D7363) are not severity-11 codes
  // and fall through untouched, so a normal debug session or a caught exception does not spam
  // stderr.
  const bool fatal = (code & 0xC0000000UL) == 0xC0000000UL;
  if (fatal) {
    writeReportToStderr(record());
  }
  // Continue searching so WER and the launcher's own crash capture still see the exception.
  return EXCEPTION_CONTINUE_SEARCH;
}

void installNativeCrashHandler() {
  AddVectoredExceptionHandler(1, &nativeCrashHandler);
}

#else

// Re-entrant so a fault inside the handler itself falls through to the default, which the launcher
// and the crash reporter can still see.
std::atomic<bool> handlingSignal{false};
void nativeCrashHandler(int number) {
  if (handlingSignal.exchange(true)) {
    return;
  }
  writeReportToStderr(record());
  handlingSignal.store(false);
  std::signal(number, SIG_DFL);
  std::raise(number);
}

void installNativeCrashHandler() {
  for (const int number : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) {
    std::signal(number, &nativeCrashHandler);
  }
}

#endif

} // namespace

void gpuInstallCrashReporter() {
  Record& rec = record();
  {
    const std::lock_guard lock(rec.mutex);
    if (rec.installed) {
      return;
    }
    rec.installed = true;
  }
  previousTerminate = std::set_terminate(&crashTerminate);
  installNativeCrashHandler();
  core::logDebug("gpu crash reporter installed");
}

} // namespace dwe::rhi
