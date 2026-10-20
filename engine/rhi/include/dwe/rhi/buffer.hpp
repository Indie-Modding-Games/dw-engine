#pragma once

#include <dwe/core/error.hpp>
#include <dwe/rhi/device.hpp>
#include <dwe/rhi/types.hpp>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <span>

VK_DEFINE_HANDLE(VmaAllocation)

namespace dwe::rhi {

// What a buffer is used for. Every buffer also has a device address, for shaders that read it through a pointer.
enum class BufferUsage : std::uint8_t {
    Vertex = 1U << 0U,
    Index = 1U << 1U,
    Uniform = 1U << 2U,
    Storage = 1U << 3U,
};

template <>
inline constexpr bool IsFlagEnum<BufferUsage> = true;

enum class MemoryLocation : std::uint8_t {
    // GPU memory, filled with an upload (Buffer::createWithData). For data that does not change.
    Device,
    // Memory the CPU writes directly through mapped(), coherent with the GPU. For data written often.
    Host,
};

struct BufferDesc {
    std::size_t size = 0;
    BufferUsage usage = BufferUsage::Storage;
    MemoryLocation location = MemoryLocation::Device;
};

// A GPU buffer and its memory. It may be dropped while frames that use it are in flight, because its Vulkan objects
// go through Device::destroyLater, but it must not outlive the device.
class Buffer {
public:
    static core::Result<Buffer> create(Device& device, const BufferDesc& desc);

    // Creates a buffer in GPU memory holding data, and waits for the upload to finish. For load time.
    static core::Result<Buffer> createWithData(Device& device, BufferUsage usage, std::span<const std::byte> data);

    // Creates a host buffer (MemoryLocation::Host) holding data, without waiting for the GPU: for buffers made while
    // the game runs, such as UI geometry. Drawable at once, also within the frame being recorded.
    static core::Result<Buffer> createMapped(Device& device, BufferUsage usage, std::span<const std::byte> data);

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;
    ~Buffer();

    [[nodiscard]] VkBuffer native() const { return buffer_; }
    [[nodiscard]] std::size_t size() const { return size_; }
    // The address shaders read the buffer at, through a pointer passed in push constants.
    [[nodiscard]] std::uint64_t address() const { return address_; }
    // The buffer's memory, for MemoryLocation::Host buffers; empty for the others.
    [[nodiscard]] std::span<std::byte> mapped() const { return mapped_; }

private:
    explicit Buffer(Device& device) : device_(&device) {}

    void release();

    Device* device_ = nullptr;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    std::size_t size_ = 0;
    std::uint64_t address_ = 0;
    std::span<std::byte> mapped_;
};

} // namespace dwe::rhi
