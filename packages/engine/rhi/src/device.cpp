#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/rhi/device.hpp>

#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <utility>
#include <vector>

#include "renderer_requirements.hpp"
#include "transition.hpp"

namespace dwe::rhi {
namespace {

// UNORM rather than SRGB, because the debug UI writes colors that are already in sRGB.
constexpr VkSurfaceFormatKHR SwapchainFormat{.format = VK_FORMAT_B8G8R8A8_UNORM,
                                             .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};

// The warnings and errors the validation layer reported, for every device of the process (the
// callback has no device): scripted runs fail when it is not zero.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): the layer's callback has no user data to reach.
std::atomic<std::uint64_t> validationMessages{0};

const char* presentModeName(VkPresentModeKHR mode) {
  switch (mode) {
  case VK_PRESENT_MODE_FIFO_KHR:
    return "fifo (vsync)";
  case VK_PRESENT_MODE_MAILBOX_KHR:
    return "mailbox";
  case VK_PRESENT_MODE_IMMEDIATE_KHR:
    return "immediate";
  default:
    return "other present mode";
  }
}

VkBool32 VKAPI_PTR logValidationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                        VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                        const VkDebugUtilsMessengerCallbackDataEXT* data,
                                        void* /*user*/) {
  if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
    validationMessages.fetch_add(1, std::memory_order_relaxed);
    core::logError("vulkan: {}", data->pMessage);
  } else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) {
    validationMessages.fetch_add(1, std::memory_order_relaxed);
    core::logWarning("vulkan: {}", data->pMessage);
  } else {
    core::logDebug("vulkan: {}", data->pMessage);
  }
  return VK_FALSE;
}

struct FrameSlot {
  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandBuffer commands = VK_NULL_HANDLE;
  VkSemaphore imageAcquired = VK_NULL_HANDLE;
  VkFence finished = VK_NULL_HANDLE;
  // The number of the frame last submitted from this slot, which its fence waits for.
  std::uint64_t submittedFrame = 0;
};

// Something released while the GPU may still use it, and the number of the last frame that may.
struct Retired {
  std::uint64_t lastFrame;
  std::function<void()> destroy;
};

} // namespace

void transitionImage(VkCommandBuffer commands, VkImage image, VkImageLayout from, VkImageLayout to,
                     VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                     VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
  VkImageMemoryBarrier2 barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  barrier.srcStageMask = srcStage;
  barrier.srcAccessMask = srcAccess;
  barrier.dstStageMask = dstStage;
  barrier.dstAccessMask = dstAccess;
  barrier.oldLayout = from;
  barrier.newLayout = to;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                              .baseMipLevel = 0,
                              .levelCount = VK_REMAINING_MIP_LEVELS,
                              .baseArrayLayer = 0,
                              .layerCount = 1};

  VkDependencyInfo dependency{};
  dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependency.imageMemoryBarrierCount = 1;
  dependency.pImageMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(commands, &dependency);
}

struct Device::Impl {
  const platform::Window* window = nullptr;
  bool vsync = true;
  // The validation layer was requested and is installed.
  bool validation = false;

  vkb::Instance instance;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  vkb::PhysicalDevice physicalDevice;
  vkb::Device device;
  VkQueue queue = VK_NULL_HANDLE;
  std::uint32_t queueFamily = 0;
  VmaAllocator allocator = VK_NULL_HANDLE;

  vkb::Swapchain swapchain;
  std::vector<VkImage> images;
  std::vector<VkImageView> views;
  // One per swapchain image: presentation waits on the semaphore of the image it presents.
  std::vector<VkSemaphore> renderFinished;
  bool swapchainStale = false;
  // Whether the swapchain images can be copied from (FrameCapture).
  bool swapchainCopyable = false;
  // The window size the swapchain was made for. The swapchain's own extent can differ (the surface
  // may clamp it), so comparing against it could rebuild the swapchain on every frame.
  platform::PixelSize swapchainWindowSize{.width = 0, .height = 0};

  std::array<FrameSlot, FramesInFlight> frames{};
  std::size_t frameIndex = 0;
  // Frames are numbered from 1 in the order they are submitted.
  std::uint64_t submittedFrames = 0;
  // Ordered by lastFrame, which never decreases.
  std::deque<Retired> retired;

  core::Result<void> createSwapchain();
  void destroySwapchainViews();
  core::Result<void> createFrames();
  // Runs what was retired for the frames up to completedFrame, which the GPU has finished.
  void destroyRetired(std::uint64_t completedFrame);

  Impl() = default;
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;
  ~Impl();
};

core::Result<void> Device::Impl::createSwapchain() {
  const platform::PixelSize size = window->pixelSize();
  // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization): Vulkan fills it in.
  VkSurfaceCapabilitiesKHR capabilities{};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
  // Screenshots of scripted runs copy the presented image; every desktop driver allows it, but it
  // is optional.
  swapchainCopyable = (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
  VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (swapchainCopyable) {
    usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  }
  vkb::SwapchainBuilder builder{device, surface};
  auto built =
      builder
          .set_desired_format(SwapchainFormat)
          // Some drivers offer no BGRA format; the renderer handles both.
          .add_fallback_format(
              {.format = VK_FORMAT_R8G8B8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
          .set_desired_present_mode(vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR)
          // Without vsync, a driver without mailbox (common on X11) presents at once, tearing,
          // rather than waiting.
          .add_fallback_present_mode(vsync ? VK_PRESENT_MODE_FIFO_KHR
                                           : VK_PRESENT_MODE_IMMEDIATE_KHR)
          .add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR)
          .set_desired_extent(static_cast<std::uint32_t>(size.width),
                              static_cast<std::uint32_t>(size.height))
          .set_image_usage_flags(usage)
          .set_old_swapchain(swapchain)
          .build();
  if (!built) {
    return core::fail(std::format("could not create the swapchain: {}", built.error().message()));
  }

  destroySwapchainViews();
  vkb::destroy_swapchain(swapchain);
  swapchain = built.value();
  images = swapchain.get_images().value();
  views = swapchain.get_image_views().value();

  for (VkSemaphore semaphore : renderFinished) {
    vkDestroySemaphore(device, semaphore, nullptr);
  }
  renderFinished.assign(images.size(), VK_NULL_HANDLE);
  VkSemaphoreCreateInfo semaphoreInfo{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0};
  for (VkSemaphore& semaphore : renderFinished) {
    vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore);
  }
  swapchainStale = false;
  swapchainWindowSize = size;
  core::logInfo("swapchain {}x{}, {} images, {}", swapchain.extent.width, swapchain.extent.height,
                images.size(), presentModeName(swapchain.present_mode));
  return {};
}

void Device::Impl::destroySwapchainViews() {
  if (!views.empty()) {
    swapchain.destroy_image_views(views);
    views.clear();
  }
}

core::Result<void> Device::Impl::createFrames() {
  for (FrameSlot& frame : frames) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamily;
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &frame.pool) != VK_SUCCESS) {
      return core::fail("could not create a command pool");
    }

    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = frame.pool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    vkAllocateCommandBuffers(device, &allocateInfo, &frame.commands);

    VkSemaphoreCreateInfo semaphoreInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0};
    vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.imageAcquired);

    VkFenceCreateInfo fenceInfo{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                .pNext = nullptr,
                                .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    vkCreateFence(device, &fenceInfo, nullptr, &frame.finished);
  }
  return {};
}

void Device::Impl::destroyRetired(std::uint64_t completedFrame) {
  while (!retired.empty() && retired.front().lastFrame <= completedFrame) {
    // Popped before running, so a destroy function may retire more.
    const std::function<void()> destroy = std::move(retired.front().destroy);
    retired.pop_front();
    destroy();
  }
}

Device::Impl::~Impl() {
  if (device.device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device);
    destroyRetired(submittedFrames + 1);
    vmaDestroyAllocator(allocator);
    for (FrameSlot& frame : frames) {
      vkDestroyFence(device, frame.finished, nullptr);
      vkDestroySemaphore(device, frame.imageAcquired, nullptr);
      vkDestroyCommandPool(device, frame.pool, nullptr);
    }
    for (VkSemaphore semaphore : renderFinished) {
      vkDestroySemaphore(device, semaphore, nullptr);
    }
    destroySwapchainViews();
    vkb::destroy_swapchain(swapchain);
    vkb::destroy_device(device);
  }
  if (surface != VK_NULL_HANDLE) {
    vkb::destroy_surface(instance, surface);
  }
  if (instance.instance != VK_NULL_HANDLE) {
    vkb::destroy_instance(instance);
  }
}

core::Result<std::unique_ptr<Device>> Device::create(const platform::Window& window,
                                                     const DeviceDesc& desc) {
  auto impl = std::make_unique<Impl>();
  impl->window = &window;
  impl->vsync = desc.vsync;

  Uint32 extensionCount = 0;
  const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
  if (extensions == nullptr) {
    return core::fail(std::format("Vulkan is not available: {}", SDL_GetError()));
  }

  vkb::InstanceBuilder instanceBuilder;
  instanceBuilder.set_app_name("dwe").set_engine_name("dwe").require_api_version(1, 3, 0);
  for (Uint32 i = 0; i < extensionCount; ++i) {
    instanceBuilder.enable_extension(extensions[i]);
  }
  if (desc.validation) {
    // Builds without the validation feature, and players' machines, may lack the layer: run without
    // it.
    const auto system = vkb::SystemInfo::get_system_info();
    impl->validation = system && system->validation_layers_available;
    if (impl->validation) {
      instanceBuilder.enable_validation_layers(true).set_debug_callback(logValidationMessage);
    } else {
      core::logWarning("vulkan: validation layer requested but not installed, running without it");
    }
  }
  auto instance = instanceBuilder.build();
  if (!instance) {
    return core::fail(
        std::format("could not create the Vulkan instance: {}", instance.error().message()));
  }
  impl->instance = instance.value();

  if (!SDL_Vulkan_CreateSurface(window.sdlWindow(), impl->instance, nullptr, &impl->surface)) {
    return core::fail(std::format("could not create the window surface: {}", SDL_GetError()));
  }

  vkb::PhysicalDeviceSelector selector{impl->instance};
  requireRendererFeatures(selector);
  auto physicalDevice = selector.set_surface(impl->surface).select();
  if (!physicalDevice) {
    return core::fail(std::format("no GPU supports Vulkan 1.3 with this window: {}",
                                  physicalDevice.error().message()));
  }
  impl->physicalDevice = physicalDevice.value();
  core::logInfo("GPU: {}", impl->physicalDevice.name);

  auto device = vkb::DeviceBuilder{impl->physicalDevice}.build();
  if (!device) {
    return core::fail(
        std::format("could not create the Vulkan device: {}", device.error().message()));
  }
  impl->device = device.value();

  auto queue = impl->device.get_queue(vkb::QueueType::graphics);
  auto queueFamily = impl->device.get_queue_index(vkb::QueueType::graphics);
  if (!queue || !queueFamily) {
    return core::fail("the GPU has no graphics queue");
  }
  impl->queue = queue.value();
  impl->queueFamily = queueFamily.value();

  VkBool32 canPresent = VK_FALSE;
  vkGetPhysicalDeviceSurfaceSupportKHR(impl->physicalDevice, impl->queueFamily, impl->surface,
                                       &canPresent);
  if (canPresent == VK_FALSE) {
    return core::fail("the graphics queue cannot present to the window");
  }

  VmaAllocatorCreateInfo allocatorInfo{};
  allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
  allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
  allocatorInfo.instance = impl->instance;
  allocatorInfo.physicalDevice = impl->physicalDevice;
  allocatorInfo.device = impl->device;
  if (vmaCreateAllocator(&allocatorInfo, &impl->allocator) != VK_SUCCESS) {
    return core::fail("could not create the GPU memory allocator");
  }

  if (auto created = impl->createSwapchain(); !created) {
    return std::unexpected{created.error()};
  }
  if (auto created = impl->createFrames(); !created) {
    return std::unexpected{created.error()};
  }
  return std::unique_ptr<Device>{new Device{std::move(impl)}};
}

Device::Device(std::unique_ptr<Impl> impl) : impl_{std::move(impl)} {}

Device::~Device() = default;

std::optional<Frame> Device::beginFrame() {
  Impl& impl = *impl_;
  FrameSlot& slot = impl.frames[impl.frameIndex];
  vkWaitForFences(impl.device, 1, &slot.finished, VK_TRUE, UINT64_MAX);
  // A fence also covers everything submitted before it, so every frame up to this slot's is done.
  impl.destroyRetired(slot.submittedFrame);

  const platform::PixelSize size = impl.window->pixelSize();
  if (size.width <= 0 || size.height <= 0) {
    return std::nullopt;
  }
  const bool resized = size.width != impl.swapchainWindowSize.width ||
                       size.height != impl.swapchainWindowSize.height;
  if (impl.swapchainStale || resized) {
    vkDeviceWaitIdle(impl.device);
    if (auto created = impl.createSwapchain(); !created) {
      core::logError("{}", created.error().message);
      return std::nullopt;
    }
  }

  std::uint32_t imageIndex = 0;
  const VkResult acquired = vkAcquireNextImageKHR(impl.device, impl.swapchain, UINT64_MAX,
                                                  slot.imageAcquired, VK_NULL_HANDLE, &imageIndex);
  if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
    impl.swapchainStale = true;
    return std::nullopt;
  }
  DWE_ASSERT(acquired == VK_SUCCESS || acquired == VK_SUBOPTIMAL_KHR);
  if (acquired == VK_SUBOPTIMAL_KHR) {
    impl.swapchainStale = true;
  }

  vkResetFences(impl.device, 1, &slot.finished);
  vkResetCommandPool(impl.device, slot.pool, 0);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(slot.commands, &beginInfo);

  Frame frame{.commands = slot.commands,
              .image = impl.images[imageIndex],
              .view = impl.views[imageIndex],
              .extent = impl.swapchain.extent,
              .imageIndex = imageIndex,
              .slot = static_cast<std::uint32_t>(impl.frameIndex)};
  transitionImage(frame.commands, frame.image, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
  return frame;
}

void Device::beginRendering(const Frame& frame, ClearColor clear) {
  DWE_ASSERT(frame.commands == impl_->frames[impl_->frameIndex].commands);
  VkRenderingAttachmentInfo color{};
  color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  color.imageView = frame.view;
  color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.clearValue.color = {{clear.red, clear.green, clear.blue, clear.alpha}};

  VkRenderingInfo rendering{};
  rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  rendering.renderArea = {.offset = {.x = 0, .y = 0}, .extent = frame.extent};
  rendering.layerCount = 1;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachments = &color;
  vkCmdBeginRendering(frame.commands, &rendering);
}

void Device::endRendering(const Frame& frame) {
  DWE_ASSERT(frame.commands == impl_->frames[impl_->frameIndex].commands);
  vkCmdEndRendering(frame.commands);
}

void Device::endFrame(const Frame& frame) {
  Impl& impl = *impl_;
  FrameSlot& slot = impl.frames[impl.frameIndex];

  transitionImage(frame.commands, frame.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE,
                  VK_ACCESS_2_NONE);
  vkEndCommandBuffer(frame.commands);

  VkSemaphore renderFinished = impl.renderFinished[frame.imageIndex];

  VkSemaphoreSubmitInfo wait{};
  wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
  wait.semaphore = slot.imageAcquired;
  wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

  VkSemaphoreSubmitInfo signal{};
  signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
  signal.semaphore = renderFinished;
  signal.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;

  VkCommandBufferSubmitInfo commands{};
  commands.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
  commands.commandBuffer = frame.commands;

  VkSubmitInfo2 submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
  submit.waitSemaphoreInfoCount = 1;
  submit.pWaitSemaphoreInfos = &wait;
  submit.commandBufferInfoCount = 1;
  submit.pCommandBufferInfos = &commands;
  submit.signalSemaphoreInfoCount = 1;
  submit.pSignalSemaphoreInfos = &signal;
  vkQueueSubmit2(impl.queue, 1, &submit, slot.finished);
  slot.submittedFrame = ++impl.submittedFrames;

  VkSwapchainKHR swapchain = impl.swapchain;
  VkPresentInfoKHR present{};
  present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores = &renderFinished;
  present.swapchainCount = 1;
  present.pSwapchains = &swapchain;
  present.pImageIndices = &frame.imageIndex;
  const VkResult presented = vkQueuePresentKHR(impl.queue, &present);
  if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
    impl.swapchainStale = true;
  }

  impl.frameIndex = (impl.frameIndex + 1) % FramesInFlight;
}

void Device::destroyLater(std::function<void()> destroy) {
  // The frame being recorded, or the next one if none is, may still draw with what the caller
  // released.
  impl_->retired.push_back(
      Retired{.lastFrame = impl_->submittedFrames + 1, .destroy = std::move(destroy)});
}

void Device::waitIdle() {
  vkDeviceWaitIdle(impl_->device);
  impl_->destroyRetired(impl_->submittedFrames + 1);
}

bool Device::supportsBc7() const {
  for (const VkFormat format : {VK_FORMAT_BC7_UNORM_BLOCK, VK_FORMAT_BC7_SRGB_BLOCK}) {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(impl_->physicalDevice, format, &properties);
    if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0) {
      return false;
    }
  }
  return true;
}

bool Device::validationEnabled() const {
  return impl_->validation && impl_->instance.debug_messenger != VK_NULL_HANDLE;
}

void Device::setVsync(bool vsync) {
  if (impl_->vsync != vsync) {
    impl_->vsync = vsync;
    impl_->swapchainStale = true;
  }
}

bool Device::vsync() const {
  return impl_->vsync;
}

std::uint64_t Device::validationMessageCount() {
  return validationMessages.load(std::memory_order_relaxed);
}

bool Device::canCaptureFrames() const {
  return impl_->swapchainCopyable;
}

VmaAllocator Device::allocator() const {
  return impl_->allocator;
}

std::uint64_t Device::allocatedBytes() const {
  const VkPhysicalDeviceMemoryProperties* memory = nullptr;
  vmaGetMemoryProperties(impl_->allocator, &memory);
  std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> budgets{};
  vmaGetHeapBudgets(impl_->allocator, budgets.data());
  std::uint64_t bytes = 0;
  for (std::uint32_t heap = 0; heap < memory->memoryHeapCount; ++heap) {
    bytes += budgets.at(heap).statistics.allocationBytes;
  }
  return bytes;
}

NativeHandles Device::nativeHandles() const {
  const Impl& impl = *impl_;
  return NativeHandles{
      .instance = impl.instance,
      .physicalDevice = impl.physicalDevice,
      .device = impl.device,
      .queueFamily = impl.queueFamily,
      .queue = impl.queue,
      .swapchainFormat = impl.swapchain.image_format,
      .imageCount = static_cast<std::uint32_t>(impl.images.size()),
      .apiVersion = VK_API_VERSION_1_3,
  };
}

} // namespace dwe::rhi

#ifdef __has_feature
#if __has_feature(address_sanitizer)
// vk-bootstrap is a C++ library from vcpkg built without AddressSanitizer. Its copies of libc++
// templates, such as std::vector<std::string> growth, can win over the project's instrumented ones
// at link time; mixing the two leaves the container annotations out of step and AddressSanitizer
// reports container overflows that are not there. Container-overflow detection is therefore off in
// executables that link the device, which are the only ones that link vk-bootstrap; everything else
// keeps it (docs/architecture/render.md). ASAN_OPTIONS still overrides this.
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming,misc-use-internal-linkage)
extern "C" const char* __asan_default_options() {
  return "detect_container_overflow=0";
}
#endif
#endif
