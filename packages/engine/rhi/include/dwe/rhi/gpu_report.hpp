#pragma once

#include <dwe/rhi/device.hpp>

#include <span>
#include <string>
#include <string_view>

namespace dwe::rhi {

// A record of the GPU the renderer runs on and the work it was doing, so that a graphics failure
// leaves a report that names them. A Vulkan error, a lost device, a fault that brings the program
// down inside a driver (the AMD proprietary compiler, an Intel iGPU driver) and a hung GPU all come
// back to this: gpuReportDevice says what the machine is, gpuNoteActivity and gpuCheck keep a trail
// of the calls and the work in flight, and the crash handlers in gpuInstallCrashReporter write the
// trail out when the process dies before it could log it.
//
// Everything here is process-wide (like the validation message counter): the trail is read by a
// crash handler that has no Device to ask.

// Records the GPU, its driver (vendor, driver id and version, decoded) and the limits the renderer
// cares about (the ones AMD and Intel drivers crash on), and writes a summary line to the log. Call
// once, after the device is created. `validation` is whether the validation layer is on.
void gpuReportDevice(const NativeHandles& handles, bool validation);

// Says what the renderer is about to do (building a pipeline, submitting a frame, starting a pass)
// so a crash names what was in flight. Cheap: it only updates a short label, kept for the report.
void gpuNoteActivity(std::string_view activity);

// The current activity label, for callers that want it in their own message.
std::string gpuActivity();

// Wraps a Vulkan call whose VkResult matters. It records the call and its result in the trail and,
// when the result is a failure, writes the GPU, driver, activity and recent trail to the error log.
// A VK_ERROR_DEVICE_LOST is recorded so beginFrame stops drawing (the device cannot be used again)
// and the message says so. Returns `result` unchanged, so callers can keep their own handling.
VkResult gpuCheck(std::string_view call, VkResult result);

// Whether a VK_ERROR_DEVICE_LOST has been seen: the device is gone, no further drawing is possible.
bool gpuDeviceLost();

// The whole report (GPU, driver, limits, activity, recent calls) as text, for a crash dump or a
// log.
std::string gpuFailureReport();

// A small, stable hash of a SPIR-V module (FNV-1a): names the shader that broke a driver's compiler
// so a player's report can be matched to an allow-list without shipping the shader's source.
std::uint64_t gpuSpirvHash(std::span<const std::uint32_t> spirv);

// Installs handlers so an uncaught C++ exception, a fatal signal (Linux) or an access violation
// raised inside a graphics driver (Windows) writes gpuFailureReport() to the log and to stderr
// before the process dies. The first call wins; later calls do nothing. It does not stop WER or the
// launcher's own crash capture: the handler returns so they still run. Safe on any thread.
void gpuInstallCrashReporter();

} // namespace dwe::rhi
