#pragma once

#include <dwe/core/error.hpp>
#include <dwe/platform/window.hpp>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

// VMA's allocator handle, declared as vk_mem_alloc.h does, so users of this header need not include
// VMA.
VK_DEFINE_HANDLE(VmaAllocator)

namespace dwe::rhi {

// Two frames in flight: the CPU records one while the GPU draws the other.
inline constexpr std::size_t FramesInFlight = 2;

struct DeviceDesc {
  // Enables the Khronos validation layer when it is installed, and logs what it reports.
  bool validation = false;
  // Waits for the display refresh before presenting; without it frames are presented as soon as
  // they are ready.
  bool vsync = true;
};

// One frame being recorded: its command buffer and the swapchain image it draws into.
struct Frame {
  VkCommandBuffer commands;
  VkImage image;
  VkImageView view;
  VkExtent2D extent;
  std::uint32_t imageIndex;
  // Which of the FramesInFlight slots records this frame. The GPU has finished the previous frame
  // of the same slot, so per-frame resources indexed by it (such as the upload ring) are free to
  // reuse.
  std::uint32_t slot;
};

struct ClearColor {
  float red = 0.0F;
  float green = 0.0F;
  float blue = 0.0F;
  float alpha = 1.0F;
};

// The raw Vulkan objects, for third-party renderers (the debug UI) that set themselves up with
// them.
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

// The GPU, its queue and the swapchain of one window. Frames are recorded with dynamic rendering,
// and the swapchain is rebuilt on its own when the window changes size.
class Device {
public:
  static core::Result<std::unique_ptr<Device>> create(const platform::Window& window,
                                                      const DeviceDesc& desc);

  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;
  Device(Device&&) = delete;
  Device& operator=(Device&&) = delete;
  ~Device();

  // Waits for a free frame and acquires the next swapchain image. Returns nothing when there is
  // nothing to draw into (the window is minimized, or the swapchain was just rebuilt); the caller
  // then skips the frame.
  std::optional<Frame> beginFrame();

  // Starts drawing into the frame's image, cleared to the given color.
  void beginRendering(const Frame& frame, ClearColor clear);
  void endRendering(const Frame& frame);

  // Submits the frame and presents its image.
  void endFrame(const Frame& frame);

  // Runs destroy once the GPU has finished every frame that may use what it destroys: the frames
  // submitted so far and the one being recorded (or the next one, between frames). For objects
  // released while frames are in flight.
  void destroyLater(std::function<void()> destroy);

  // Waits until the GPU has finished all submitted work, then runs everything passed to
  // destroyLater. Call it between frames, never while one is being recorded.
  void waitIdle();

  // Returns true if the GPU samples BC7 textures, in both their UNORM and sRGB formats. Every
  // desktop GPU does; without it, textures are uploaded as RGBA8.
  [[nodiscard]] bool supportsBc7() const;

  [[nodiscard]] NativeHandles nativeHandles() const;

  // Whether the validation layer is on: requested in DeviceDesc and installed.
  [[nodiscard]] bool validationEnabled() const;

  // How many warnings and errors the validation layer has reported since the program started, by
  // every device.
  [[nodiscard]] static std::uint64_t validationMessageCount();

  // Whether the swapchain images may be copied to the CPU (FrameCapture).
  [[nodiscard]] bool canCaptureFrames() const;

  // The memory allocator (VMA) of the device's buffers and images.
  [[nodiscard]] VmaAllocator allocator() const;

private:
  struct Impl;

  explicit Device(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
};

} // namespace dwe::rhi
