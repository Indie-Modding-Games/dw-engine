#pragma once

#include <dwe/core/error.hpp>
#include <dwe/platform/window.hpp>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace dwe::rhi {

struct DeviceDesc {
    // Enables the Khronos validation layer when it is installed, and logs what it reports.
    bool validation = false;
    // Waits for the display refresh before presenting; without it frames are presented as soon as they are ready.
    bool vsync = true;
};

// One frame being recorded: its command buffer and the swapchain image it draws into.
struct Frame {
    VkCommandBuffer commands;
    VkImage image;
    VkImageView view;
    VkExtent2D extent;
    std::uint32_t imageIndex;
};

struct ClearColor {
    float red;
    float green;
    float blue;
    float alpha = 1.0F;
};

// The raw Vulkan objects, for third-party renderers (the debug UI) that set themselves up with them.
struct NativeHandles {
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    std::uint32_t queueFamily;
    VkQueue queue;
    VkFormat swapchainFormat;
    std::uint32_t imageCount;
    std::uint32_t apiVersion;
};

// The GPU, its queue and the swapchain of one window. Frames are recorded with dynamic rendering, and the
// swapchain is rebuilt on its own when the window changes size.
class Device {
public:
    static core::Result<std::unique_ptr<Device>> create(const platform::Window& window, const DeviceDesc& desc);

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    ~Device();

    // Waits for a free frame and acquires the next swapchain image. Returns nothing when there is nothing to draw
    // into (the window is minimized, or the swapchain was just rebuilt); the caller then skips the frame.
    std::optional<Frame> beginFrame();

    // Starts drawing into the frame's image, cleared to the given color.
    void beginRendering(const Frame& frame, ClearColor clear);
    void endRendering(const Frame& frame);

    // Submits the frame and presents its image.
    void endFrame(const Frame& frame);

    // Waits until the GPU has finished all submitted work.
    void waitIdle();

    [[nodiscard]] NativeHandles nativeHandles() const;

private:
    struct Impl;

    explicit Device(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

} // namespace dwe::rhi
