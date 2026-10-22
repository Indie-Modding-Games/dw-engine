#pragma once

#include <dwe/core/error.hpp>
#include <dwe/rhi/buffer.hpp>
#include <dwe/rhi/device.hpp>
#include <dwe/rhi/linear_allocator.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace dwe::rhi {

// Part of the upload ring, written by the CPU and read by the GPU during the frame it was allocated in.
struct UploadAllocation {
    // Where to write the data.
    std::span<std::byte> data;
    // The ring's buffer and the offset of data in it, to bind as a vertex or index buffer.
    const Buffer* buffer = nullptr;
    std::size_t offset = 0;
    // The device address of data, for shaders that read it through a pointer.
    std::uint64_t address = 0;
};

// Memory for data that changes every frame (joint matrices, UI vertices): one host-visible buffer split into one part
// per frame in flight. What is allocated during a frame stays valid until that frame's slot comes round again, when
// the GPU has finished with it. Allocations are only possible between beginFrame and endFrame, while the slot is
// known to be free: the renderer hands the ring out only to drawers during a frame.
class UploadRing {
public:
    // Default alignment: enough for any vertex attribute and for std430 matrices and vectors.
    static constexpr std::size_t DefaultAlignment = 16;

    static core::Result<UploadRing> create(Device& device, std::size_t bytesPerFrame);

    // Starts the frame recorded in slot (Frame::slot), freeing what that slot held.
    void beginFrame(std::uint32_t slot);
    // Ends the frame: nothing may be allocated until the next beginFrame.
    void endFrame() { inFrame_ = false; }
    [[nodiscard]] bool inFrame() const { return inFrame_; }

    // Room for size bytes in the current frame, or nothing when this frame's part is full. Only within a frame.
    std::optional<UploadAllocation> allocate(std::size_t size, std::size_t alignment = DefaultAlignment);

    // Copies values into the current frame's part.
    template <typename T>
        requires std::is_trivially_copyable_v<T>
    std::optional<UploadAllocation> upload(std::span<const T> values, std::size_t alignment = DefaultAlignment) {
        auto allocation = allocate(values.size_bytes(), alignment);
        if (allocation && !values.empty()) {
            std::memcpy(allocation->data.data(), values.data(), values.size_bytes());
        }
        return allocation;
    }

    // Whether allocate(size, alignment) would find room in the current frame's part, for a caller that waits for the
    // next frame instead of failing.
    [[nodiscard]] bool fits(std::size_t size, std::size_t alignment = DefaultAlignment) const {
        return frames_[slot_].fits(size, alignment);
    }

    [[nodiscard]] std::size_t bytesPerFrame() const { return bytesPerFrame_; }
    // Bytes allocated so far in the current frame.
    [[nodiscard]] std::size_t used() const { return frames_[slot_].used(); }

private:
    UploadRing(Buffer buffer, std::size_t bytesPerFrame);

    Buffer buffer_;
    std::size_t bytesPerFrame_;
    std::vector<LinearAllocator> frames_;
    std::uint32_t slot_ = 0;
    bool inFrame_ = false;
};

} // namespace dwe::rhi
