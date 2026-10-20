#pragma once

#include <dwe/core/error.hpp>
#include <dwe/rhi/device.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

VK_DEFINE_HANDLE(VmaAllocation)

namespace dwe::rhi {

// The pixels of a frame as it was presented: 8-bit RGBA, row by row from the top left, alpha always opaque.
struct CapturedImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::byte> pixels;
};

// Reads a frame back to the CPU, for the screenshots of scripted runs (dwe-game --autotest). Not for every frame:
// reading waits for the GPU to finish all its work.
//
//   auto capture = FrameCapture::record(device, frame); // after the frame's last rendering, before endFrame
//   device.endFrame(frame);
//   auto image = capture->read();                        // waits for the GPU
class FrameCapture {
public:
    // Records a copy of the frame's image into a buffer the CPU can read. Fails when the device cannot copy its
    // swapchain images (Device::canCaptureFrames) or the buffer cannot be made.
    static core::Result<FrameCapture> record(Device& device, const Frame& frame);

    FrameCapture(const FrameCapture&) = delete;
    FrameCapture& operator=(const FrameCapture&) = delete;
    FrameCapture(FrameCapture&& other) noexcept;
    FrameCapture& operator=(FrameCapture&& other) noexcept;
    ~FrameCapture();

    // Waits until the GPU has finished every submitted frame, then converts the copy to RGBA. Call it after the
    // frame was submitted (Device::endFrame).
    core::Result<CapturedImage> read();

private:
    explicit FrameCapture(Device& device) : device_(&device) {}

    void release();

    Device* device_ = nullptr;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

} // namespace dwe::rhi
