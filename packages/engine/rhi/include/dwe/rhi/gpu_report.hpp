#pragma once

#include <dwe/rhi/device.hpp>

#include <cstdint>
#include <span>
#include <string_view>

namespace dwe::rhi {

// A process-wide record of the GPU and of the last Vulkan calls, so a graphics failure, a lost
// device or a crash inside a driver leaves a report that names them. Process-wide because the crash
// handlers that print it have no Device to ask.

// The optional breadcrumbs a device enabled, read back when it is lost.
struct GpuDiagnostics {
  bool deviceFault = false;   // VK_EXT_device_fault
  bool bufferMarkers = false; // VK_AMD_buffer_marker
};

// Work whose markers run in recording order: the frames, and the uploads that wait on their own.
enum class GpuWork : std::uint8_t { Frame, Upload };

// Records the GPU, its driver and the limits the renderer depends on, and logs them. Call once,
// after the device is created.
void gpuReportDevice(const Device& device, GpuDiagnostics diagnostics);

// Releases what gpuReportDevice took from `device`, before its allocator is destroyed.
void gpuForgetDevice(VkDevice device);

// Names what the renderer is about to do (building a pipeline, a pass, a present), for the report.
void gpuNoteActivity(std::string_view activity);

// Records in `commands` that the GPU finished the current activity, so the report of a lost device
// names the last work done and the first left undone. Does nothing without VK_AMD_buffer_marker.
void gpuMarker(VkCommandBuffer commands, GpuWork work);

// Records a Vulkan call and its result in the report's trail and logs the report when it failed.
// A VK_ERROR_DEVICE_LOST marks the device lost for good and adds the device fault and the markers
// to the report. Returns `result` unchanged.
VkResult gpuCheck(std::string_view call, VkResult result);

// Whether a VK_ERROR_DEVICE_LOST has been seen: the device can draw nothing more.
bool gpuDeviceLost();

// A stable hash of a SPIR-V module (FNV-1a), to name the shader that broke a driver's compiler.
std::uint64_t gpuSpirvHash(std::span<const std::uint32_t> spirv);

// Prints the report to stderr when the process dies of an uncaught exception, a fatal signal or an
// unhandled native exception, then hands the crash on to whatever handled it before. Only the first
// call installs.
void gpuInstallCrashReporter();

} // namespace dwe::rhi
