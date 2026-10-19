#include <dwe/rhi/texture.hpp>

#include <cstring>
#include <format>
#include <optional>
#include <utility>

#include "transition.hpp"

namespace dwe::rhi {
namespace {

// UNORM, like the swapchain: the pixels already hold sRGB values and the debug UI draws them as they are.
constexpr VkFormat TextureFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkDeviceSize BytesPerPixel = 4;

std::optional<std::uint32_t> findMemoryType(VkPhysicalDevice physicalDevice, std::uint32_t allowedTypes,
                                            VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memory);
    for (std::uint32_t type = 0; type < memory.memoryTypeCount; ++type) {
        if ((allowedTypes & (1U << type)) != 0 && (memory.memoryTypes[type].propertyFlags & properties) == properties) {
            return type;
        }
    }
    return std::nullopt;
}

core::Result<VkDeviceMemory> allocate(const NativeHandles& gpu, const VkMemoryRequirements& requirements,
                                      VkMemoryPropertyFlags properties) {
    const auto type = findMemoryType(gpu.physicalDevice, requirements.memoryTypeBits, properties);
    if (!type) {
        return core::fail("the GPU has no memory type for textures");
    }
    const VkMemoryAllocateInfo info{.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                    .pNext = nullptr,
                                    .allocationSize = requirements.size,
                                    .memoryTypeIndex = *type};
    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (const VkResult result = vkAllocateMemory(gpu.device, &info, nullptr, &memory); result != VK_SUCCESS) {
        return core::fail(std::format("could not allocate {} bytes of GPU memory (Vulkan error {})", requirements.size,
                                      static_cast<int>(result)));
    }
    return memory;
}

// The staging buffer and the one-off commands of an upload, destroyed when it ends.
struct Upload {
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    Upload() = default;
    Upload(const Upload&) = delete;
    Upload& operator=(const Upload&) = delete;
    Upload(Upload&&) = delete;
    Upload& operator=(Upload&&) = delete;
    ~Upload() {
        vkDestroyFence(device, fence, nullptr);
        vkDestroyCommandPool(device, pool, nullptr);
        vkDestroyBuffer(device, buffer, nullptr);
        vkFreeMemory(device, memory, nullptr);
    }
};

core::Result<void> fill(Upload& upload, const NativeHandles& gpu, std::span<const std::byte> pixels) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = pixels.size();
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(gpu.device, &bufferInfo, nullptr, &upload.buffer) != VK_SUCCESS) {
        return core::fail("could not create the staging buffer of a texture");
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(gpu.device, upload.buffer, &requirements);
    auto memory =
        allocate(gpu, requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!memory) {
        return std::unexpected{memory.error()};
    }
    upload.memory = *memory;
    vkBindBufferMemory(gpu.device, upload.buffer, upload.memory, 0);

    void* mapped = nullptr;
    if (vkMapMemory(gpu.device, upload.memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) {
        return core::fail("could not map the staging buffer of a texture");
    }
    std::memcpy(mapped, pixels.data(), pixels.size());
    vkUnmapMemory(gpu.device, upload.memory);
    return {};
}

core::Result<void> copyToImage(const NativeHandles& gpu, std::span<const std::byte> pixels, VkImage image,
                               VkExtent3D extent) {
    Upload upload;
    upload.device = gpu.device;
    if (auto filled = fill(upload, gpu, pixels); !filled) {
        return filled;
    }

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = gpu.queueFamily;
    if (vkCreateCommandPool(gpu.device, &poolInfo, nullptr, &upload.pool) != VK_SUCCESS) {
        return core::fail("could not create the command pool of a texture upload");
    }
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = upload.pool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(gpu.device, &allocateInfo, &commands) != VK_SUCCESS) {
        return core::fail("could not allocate the commands of a texture upload");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commands, &beginInfo);
    transitionImage(commands, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT);
    const VkBufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .mipLevel = 0,
                             .baseArrayLayer = 0,
                             .layerCount = 1},
        .imageOffset = {.x = 0, .y = 0, .z = 0},
        .imageExtent = extent,
    };
    vkCmdCopyBufferToImage(commands, upload.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    // Later submissions come after this barrier in submission order, so the frames that sample the image see the copy.
    transitionImage(commands, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    vkEndCommandBuffer(commands);

    const VkFenceCreateInfo fenceInfo{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = nullptr, .flags = 0};
    if (vkCreateFence(gpu.device, &fenceInfo, nullptr, &upload.fence) != VK_SUCCESS) {
        return core::fail("could not create the fence of a texture upload");
    }
    VkCommandBufferSubmitInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandInfo.commandBuffer = commands;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandInfo;
    if (vkQueueSubmit2(gpu.queue, 1, &submit, upload.fence) != VK_SUCCESS ||
        vkWaitForFences(gpu.device, 1, &upload.fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        return core::fail("could not upload a texture to the GPU");
    }
    return {};
}

} // namespace

core::Result<Texture> Texture::create(Device& device, const TextureDesc& desc) {
    const NativeHandles gpu = device.nativeHandles();
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(gpu.physicalDevice, &properties);
    const std::uint32_t maxSide = properties.limits.maxImageDimension2D;
    if (desc.width == 0 || desc.height == 0 || desc.width > maxSide || desc.height > maxSide) {
        return core::fail(
            std::format("a texture has 1 to {} pixels on a side; this one is {}x{}", maxSide, desc.width, desc.height));
    }
    if (const VkDeviceSize expected = VkDeviceSize{desc.width} * desc.height * BytesPerPixel;
        desc.pixels.size() != expected) {
        return core::fail(std::format("a {}x{} texture needs {} bytes of RGBA, not {}", desc.width, desc.height,
                                      expected, desc.pixels.size()));
    }

    // Whatever this holds goes back to the device if a later step fails.
    Texture texture{device, desc.width, desc.height};
    const VkExtent3D extent{.width = desc.width, .height = desc.height, .depth = 1};

    const VkImageCreateInfo imageInfo{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                      .pNext = nullptr,
                                      .flags = 0,
                                      .imageType = VK_IMAGE_TYPE_2D,
                                      .format = TextureFormat,
                                      .extent = extent,
                                      .mipLevels = 1,
                                      .arrayLayers = 1,
                                      .samples = VK_SAMPLE_COUNT_1_BIT,
                                      .tiling = VK_IMAGE_TILING_OPTIMAL,
                                      .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                                      .queueFamilyIndexCount = 0,
                                      .pQueueFamilyIndices = nullptr,
                                      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    if (vkCreateImage(gpu.device, &imageInfo, nullptr, &texture.image_) != VK_SUCCESS) {
        return core::fail(std::format("could not create a {}x{} texture", desc.width, desc.height));
    }
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(gpu.device, texture.image_, &requirements);
    auto memory = allocate(gpu, requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!memory) {
        return std::unexpected{memory.error()};
    }
    texture.memory_ = *memory;
    vkBindImageMemory(gpu.device, texture.image_, texture.memory_, 0);

    if (auto copied = copyToImage(gpu, desc.pixels, texture.image_, extent); !copied) {
        return std::unexpected{copied.error()};
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture.image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = TextureFormat;
    viewInfo.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1};
    if (vkCreateImageView(gpu.device, &viewInfo, nullptr, &texture.view_) != VK_SUCCESS) {
        return core::fail("could not create the view of a texture");
    }
    return texture;
}

Texture::Texture(Texture&& other) noexcept
    : device_(std::exchange(other.device_, nullptr)), image_(std::exchange(other.image_, VK_NULL_HANDLE)),
      memory_(std::exchange(other.memory_, VK_NULL_HANDLE)), view_(std::exchange(other.view_, VK_NULL_HANDLE)),
      width_(std::exchange(other.width_, 0)), height_(std::exchange(other.height_, 0)) {}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        release();
        device_ = std::exchange(other.device_, nullptr);
        image_ = std::exchange(other.image_, VK_NULL_HANDLE);
        memory_ = std::exchange(other.memory_, VK_NULL_HANDLE);
        view_ = std::exchange(other.view_, VK_NULL_HANDLE);
        width_ = std::exchange(other.width_, 0);
        height_ = std::exchange(other.height_, 0);
    }
    return *this;
}

Texture::~Texture() {
    release();
}

void Texture::release() {
    if (device_ == nullptr) {
        return;
    }
    device_->destroyLater([device = device_->nativeHandles().device, image = image_, memory = memory_, view = view_] {
        vkDestroyImageView(device, view, nullptr);
        vkDestroyImage(device, image, nullptr);
        vkFreeMemory(device, memory, nullptr);
    });
    device_ = nullptr;
}

} // namespace dwe::rhi
