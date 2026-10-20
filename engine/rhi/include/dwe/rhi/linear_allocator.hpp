#pragma once

#include <bit>
#include <cstddef>
#include <optional>

namespace dwe::rhi {

// Hands out aligned ranges of a fixed capacity one after the other until reset. Plain CPU bookkeeping, used for each
// frame's part of the upload ring.
class LinearAllocator {
public:
    explicit LinearAllocator(std::size_t capacity) : capacity_(capacity) {}

    // The offset of size bytes aligned to alignment (a power of two), or nothing when they do not fit.
    std::optional<std::size_t> allocate(std::size_t size, std::size_t alignment) {
        if (!std::has_single_bit(alignment)) {
            return std::nullopt;
        }
        const std::size_t offset = (used_ + alignment - 1) & ~(alignment - 1);
        if (offset > capacity_ || size > capacity_ - offset) {
            return std::nullopt;
        }
        used_ = offset + size;
        return offset;
    }

    // Makes the whole capacity free again.
    void reset() { used_ = 0; }

    [[nodiscard]] std::size_t capacity() const { return capacity_; }
    [[nodiscard]] std::size_t used() const { return used_; }

private:
    std::size_t capacity_;
    std::size_t used_ = 0;
};

} // namespace dwe::rhi
