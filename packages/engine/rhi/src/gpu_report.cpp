#include <dwe/core/log.hpp>
#include <dwe/rhi/gpu_report.hpp>

#include <vk_mem_alloc.h>
#include <vulkan/vk_enum_string_helper.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <format>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gpu_report_text.hpp"
#include "vulkan_util.hpp"

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

// Enough calls to show the uploads and the submit that led to a fault.
constexpr std::size_t TrailLength = 12;

// Labels of the newest markers of each kind of work: a few frames of passes.
constexpr std::uint32_t MarkerLabels = 64;

constexpr std::size_t WorkKinds = std::to_underlying(GpuWork::Upload) + 1;

using Line = std::array<char, 256>;

struct Markers {
  std::uint32_t recorded = 0;
  std::array<Line, MarkerLabels> labels{};
};

// Fixed buffers only: the crash handlers read them without allocating or locking.
struct Record {
  std::mutex mutex;
  Line gpu{};
  Line driver{};
  Line limits{};
  Line activity{};
  std::array<Line, TrailLength> trail{};
  std::size_t calls = 0;
  bool deviceLost = false;
  std::array<char, 4096> breadcrumbs{};

  VkDevice device = VK_NULL_HANDLE;
  PFN_vkGetDeviceFaultInfoEXT getFaultInfo = nullptr;
  PFN_vkCmdWriteBufferMarkerAMD writeMarker = nullptr;
  VmaAllocator allocator = VK_NULL_HANDLE;
  VkBuffer markerBuffer = VK_NULL_HANDLE;
  VmaAllocation markerAllocation = VK_NULL_HANDLE;
  // One slot per kind of work, which the GPU writes and the CPU reads once the device is lost.
  const volatile std::uint32_t* finished = nullptr;
  std::array<Markers, WorkKinds> markers{};
};

Record& record() {
  // Never destroyed: a crash handler may run while static objects are being torn down.
  static auto* const instance = new Record();
  return *instance;
}

template <std::size_t Size>
void store(std::array<char, Size>& line, std::string_view text) {
  const std::size_t length = std::min(text.size(), Size - 1);
  std::ranges::copy(text.substr(0, length), line.begin());
  line[length] = '\0';
}

// A string the driver filled in, which may not end in a null.
template <std::size_t Size>
std::string_view fixedText(const char (&chars)[Size]) {
  return {chars, std::ranges::find(chars, '\0')};
}

// The trail's lines, oldest first.
template <typename Visit>
void forEachCall(const Record& rec, Visit visit) {
  const std::size_t first = rec.calls < TrailLength ? 0 : rec.calls % TrailLength;
  for (std::size_t step = 0; step < std::min(rec.calls, TrailLength); ++step) {
    visit(rec.trail[(first + step) % TrailLength].data());
  }
}

std::string reportText(const Record& rec) {
  std::string text =
      std::format("gpu:      {}\ndriver:   {}\nlimits:   {}\nactivity: {}\n"
                  "recent Vulkan calls (newest last):\n",
                  rec.gpu.data(), rec.driver.data(), rec.limits.data(), rec.activity.data());
  forEachCall(rec, [&text](const char* call) { text += std::format("  {}\n", call); });
  text += rec.breadcrumbs.data();
  return text;
}

std::string faultText(const Record& rec) {
  if (rec.getFaultInfo == nullptr) {
    return {};
  }
  VkDeviceFaultCountsEXT counts{};
  counts.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT;
  if (rec.getFaultInfo(rec.device, &counts, nullptr) < 0) {
    return "device fault: the driver gave no details\n";
  }
  std::vector<VkDeviceFaultAddressInfoEXT> addresses(counts.addressInfoCount);
  std::vector<VkDeviceFaultVendorInfoEXT> vendor(counts.vendorInfoCount);
  counts.vendorBinarySize = 0;
  VkDeviceFaultInfoEXT fault{};
  fault.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT;
  fault.pAddressInfos = addresses.data();
  fault.pVendorInfos = vendor.data();
  if (rec.getFaultInfo(rec.device, &counts, &fault) < 0) {
    return "device fault: the driver gave no details\n";
  }
  return describeDeviceFault(counts, fault);
}

std::string markerText(const Record& rec) {
  if (rec.finished == nullptr) {
    return {};
  }
  std::string text;
  for (const auto [work, name] :
       {std::pair{GpuWork::Frame, "frame"}, {GpuWork::Upload, "upload"}}) {
    const auto slot = static_cast<std::size_t>(std::to_underlying(work));
    const Markers& markers = rec.markers.at(slot);
    text += describeMarkers(name, rec.finished[slot], markers.recorded,
                            [&markers](std::uint32_t marker) -> std::optional<std::string_view> {
                              if (marker + MarkerLabels <= markers.recorded) {
                                return std::nullopt;
                              }
                              return markers.labels.at(marker % MarkerLabels).data();
                            });
  }
  return text;
}

void createMarkerBuffer(Record& rec, VmaAllocator allocator) {
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = WorkKinds * sizeof(std::uint32_t);
  bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  VmaAllocationCreateInfo allocationInfo{};
  allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
  allocationInfo.flags =
      VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
  allocationInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  VmaAllocationInfo allocated{};
  if (vmaCreateBuffer(allocator, &bufferInfo, &allocationInfo, &rec.markerBuffer,
                      &rec.markerAllocation, &allocated) != VK_SUCCESS) {
    rec.writeMarker = nullptr;
    return;
  }
  rec.allocator = allocator;
  auto* const finished = static_cast<std::uint32_t*>(allocated.pMappedData);
  std::fill_n(finished, WorkKinds, 0U);
  rec.finished = finished;
}

std::atomic<bool> crashReported{false};

void writeCrashReport() {
  if (crashReported.exchange(true)) {
    return;
  }
  const Record& rec = record();
  std::fprintf(stderr,
               "=== dwe graphics crash report ===\ngpu:      %s\ndriver:   %s\nlimits:   %s\n"
               "activity: %s\nrecent Vulkan calls (newest last):\n",
               rec.gpu.data(), rec.driver.data(), rec.limits.data(), rec.activity.data());
  forEachCall(rec, [](const char* call) { std::fprintf(stderr, "  %s\n", call); });
  std::fputs(rec.breadcrumbs.data(), stderr);
  std::fflush(stderr);
}

std::terminate_handler previousTerminate = nullptr;

[[noreturn]] void crashTerminate() {
  writeCrashReport();
  if (previousTerminate != nullptr) {
    previousTerminate();
  }
  std::abort();
}

#ifdef _WIN32

LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;

LONG WINAPI crashFilter(PEXCEPTION_POINTERS pointers) {
  writeCrashReport();
  return previousFilter != nullptr ? previousFilter(pointers) : EXCEPTION_CONTINUE_SEARCH;
}

void installNativeCrashHandler() {
  previousFilter = SetUnhandledExceptionFilter(&crashFilter);
}

#else

constexpr std::array FatalSignals{SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS};
std::array<void (*)(int), FatalSignals.size()> previousHandlers{};

void crashSignal(int number) {
  writeCrashReport();
  const auto* const found = std::ranges::find(FatalSignals, number);
  const auto previous = previousHandlers.at(static_cast<std::size_t>(found - FatalSignals.begin()));
  std::signal(number, previous == SIG_ERR || previous == SIG_IGN ? SIG_DFL : previous);
  std::raise(number);
}

void installNativeCrashHandler() {
  for (std::size_t i = 0; i < FatalSignals.size(); ++i) {
    previousHandlers.at(i) = std::signal(FatalSignals.at(i), &crashSignal);
  }
}

#endif

} // namespace

void gpuReportDevice(const Device& device, GpuDiagnostics diagnostics) {
  const NativeHandles handles = device.nativeHandles();
  const GpuInfo gpu = describeGpu(handles.physicalDevice);
  // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization): Vulkan fills it in.
  VkPhysicalDeviceVulkan12Properties properties12{};
  properties12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
  VkPhysicalDeviceProperties2 properties{};
  properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  properties.pNext = &properties12;
  vkGetPhysicalDeviceProperties2(handles.physicalDevice, &properties);
  const VkPhysicalDeviceLimits& limits = properties.properties.limits;

  const std::string gpuText =
      std::format("{} (vendor {:#06x}, {})", gpu.name, gpu.vendorId, gpuKindName(gpu.kind));
  const std::string driverText =
      std::format("{}; api {}.{}.{}", gpu.driver, VK_API_VERSION_MAJOR(gpu.apiVersion),
                  VK_API_VERSION_MINOR(gpu.apiVersion), VK_API_VERSION_PATCH(gpu.apiVersion));
  const std::string limitsText = std::format(
      "max image {}, max anisotropy {}, BC7 {}, bindless images up to {}; validation {}",
      limits.maxImageDimension2D, limits.maxSamplerAnisotropy, device.supportsBc7() ? "yes" : "no",
      std::min(properties12.maxDescriptorSetUpdateAfterBindSampledImages,
               properties12.maxPerStageDescriptorUpdateAfterBindSampledImages),
      device.validationEnabled() ? "on" : "off");
  {
    Record& rec = record();
    const std::lock_guard lock(rec.mutex);
    store(rec.gpu, gpuText);
    store(rec.driver, driverText);
    store(rec.limits, limitsText);
    rec.device = handles.device;
    if (diagnostics.deviceFault) {
      rec.getFaultInfo = reinterpret_cast<PFN_vkGetDeviceFaultInfoEXT>(
          vkGetDeviceProcAddr(handles.device, "vkGetDeviceFaultInfoEXT"));
    }
    if (diagnostics.bufferMarkers) {
      rec.writeMarker = reinterpret_cast<PFN_vkCmdWriteBufferMarkerAMD>(
          vkGetDeviceProcAddr(handles.device, "vkCmdWriteBufferMarkerAMD"));
      if (rec.writeMarker != nullptr) {
        createMarkerBuffer(rec, device.allocator());
      }
    }
    diagnostics.deviceFault = rec.getFaultInfo != nullptr;
    diagnostics.bufferMarkers = rec.writeMarker != nullptr;
  }
  core::logInfo("gpu report: {}", gpuText);
  core::logInfo("gpu report: driver {}", driverText);
  core::logInfo("gpu report: {}", limitsText);
  core::logInfo("gpu report: device fault details {}, GPU markers {}",
                diagnostics.deviceFault ? "on" : "off", diagnostics.bufferMarkers ? "on" : "off");
}

void gpuForgetDevice(VkDevice device) {
  Record& rec = record();
  const std::lock_guard lock(rec.mutex);
  if (rec.device != device) {
    return;
  }
  if (rec.markerBuffer != VK_NULL_HANDLE) {
    vmaDestroyBuffer(rec.allocator, rec.markerBuffer, rec.markerAllocation);
  }
  rec.device = VK_NULL_HANDLE;
  rec.getFaultInfo = nullptr;
  rec.writeMarker = nullptr;
  rec.allocator = VK_NULL_HANDLE;
  rec.markerBuffer = VK_NULL_HANDLE;
  rec.markerAllocation = VK_NULL_HANDLE;
  rec.finished = nullptr;
}

void gpuMarker(VkCommandBuffer commands, GpuWork work) {
  Record& rec = record();
  const std::lock_guard lock(rec.mutex);
  if (rec.finished == nullptr) {
    return;
  }
  const auto slot = static_cast<std::size_t>(std::to_underlying(work));
  Markers& markers = rec.markers.at(slot);
  ++markers.recorded;
  store(markers.labels.at(markers.recorded % MarkerLabels), rec.activity.data());
  rec.writeMarker(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, rec.markerBuffer,
                  slot * sizeof(std::uint32_t), markers.recorded);
}

void gpuNoteActivity(std::string_view activity) {
  Record& rec = record();
  const std::lock_guard lock(rec.mutex);
  store(rec.activity, activity);
}

VkResult gpuCheck(std::string_view call, VkResult result) {
  // Every window resize brings these: they would push the calls that matter out of the trail.
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    return result;
  }
  std::string report;
  {
    Record& rec = record();
    const std::lock_guard lock(rec.mutex);
    store(rec.trail.at(rec.calls % TrailLength),
          std::format("{} = {}", call, string_VkResult(result)));
    ++rec.calls;
    if (result == VK_ERROR_DEVICE_LOST && !rec.deviceLost) {
      rec.deviceLost = true;
      store(rec.breadcrumbs, faultText(rec) + markerText(rec));
    }
    if (result < 0) {
      report = reportText(rec);
    }
  }
  if (result < 0) {
    core::logError("vulkan: {} failed with {}\n{}", call, string_VkResult(result), report);
  }
  return result;
}

bool gpuDeviceLost() {
  Record& rec = record();
  const std::lock_guard lock(rec.mutex);
  return rec.deviceLost;
}

std::string describeDeviceFault(const VkDeviceFaultCountsEXT& counts,
                                const VkDeviceFaultInfoEXT& fault) {
  std::string text = std::format("device fault: {}\n", fixedText(fault.description));
  for (const VkDeviceFaultAddressInfoEXT& address :
       std::span{fault.pAddressInfos, counts.addressInfoCount}) {
    const VkDeviceSize low = address.reportedAddress & ~(address.addressPrecision - 1);
    text += std::format("  {} at {:#x}..{:#x}\n",
                        string_VkDeviceFaultAddressTypeKHR(address.addressType), low,
                        low + std::max<VkDeviceSize>(address.addressPrecision, 1) - 1);
  }
  for (const VkDeviceFaultVendorInfoEXT& vendor :
       std::span{fault.pVendorInfos, counts.vendorInfoCount}) {
    text += std::format("  vendor: {} (code {:#x}, data {:#x})\n", fixedText(vendor.description),
                        vendor.vendorFaultCode, vendor.vendorFaultData);
  }
  return text;
}

std::string
describeMarkers(std::string_view work, std::uint32_t finished, std::uint32_t recorded,
                const std::function<std::optional<std::string_view>(std::uint32_t)>& label) {
  const auto named = [&label](std::uint32_t marker) {
    return std::format("#{} {}", marker, label(marker).value_or("(too old to name)"));
  };
  if (finished > recorded) {
    return std::format("{} markers: the GPU wrote #{}, past the {} recorded\n", work, finished,
                       recorded);
  }
  return std::format("{} markers: last finished {}; first unfinished {}\n", work,
                     finished == 0 ? "none" : named(finished),
                     finished == recorded ? "none" : named(finished + 1));
}

std::uint64_t gpuSpirvHash(std::span<const std::uint32_t> spirv) {
  constexpr std::uint64_t FnvOffset = 14695981039346656037ULL;
  constexpr std::uint64_t FnvPrime = 1099511628211ULL;
  std::uint64_t hash = FnvOffset;
  for (const std::uint32_t word : spirv) {
    for (std::size_t byte = 0; byte < sizeof(word); ++byte) {
      hash ^= (word >> (byte * 8)) & 0xFFU;
      hash *= FnvPrime;
    }
  }
  return hash;
}

void gpuInstallCrashReporter() {
  static std::once_flag installed;
  std::call_once(installed, [] {
    previousTerminate = std::set_terminate(&crashTerminate);
    installNativeCrashHandler();
  });
}

} // namespace dwe::rhi
