#pragma once

#include <dwe/core/error.hpp>
#include <dwe/rhi/frame_capture.hpp>

#include <filesystem>

namespace dwe::app {

// Writes a frame the renderer captured (Renderer::captureNextFrame) as a PNG file: the screenshots
// of autotests and bug reports.
core::Result<void> writeScreenshot(rhi::CapturedImage captured, const std::filesystem::path& path);

} // namespace dwe::app
