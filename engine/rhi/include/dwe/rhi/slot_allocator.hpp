#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace dwe::rhi {

// Hands out the indices 0 to capacity - 1, reusing released ones (the most recently released first). Plain CPU
// bookkeeping, used for the slots of the bindless texture table.
class SlotAllocator {
public:
    explicit SlotAllocator(std::uint32_t capacity) : capacity_(capacity), inUse_(capacity, false) {}

    // A free index, or nothing when all capacity indices are in use.
    std::optional<std::uint32_t> acquire() {
        if (!free_.empty()) {
            const std::uint32_t slot = free_.back();
            free_.pop_back();
            inUse_[slot] = true;
            return slot;
        }
        if (next_ < capacity_) {
            inUse_[next_] = true;
            return next_++;
        }
        return std::nullopt;
    }

    // Makes slot available again. Returns false, and changes nothing, when slot is not in use (released twice, or
    // never acquired).
    bool release(std::uint32_t slot) {
        if (slot >= capacity_ || !inUse_[slot]) {
            return false;
        }
        inUse_[slot] = false;
        free_.push_back(slot);
        return true;
    }

    [[nodiscard]] std::uint32_t capacity() const { return capacity_; }
    // How many indices are in use.
    [[nodiscard]] std::uint32_t used() const { return next_ - static_cast<std::uint32_t>(free_.size()); }

private:
    std::uint32_t capacity_;
    std::uint32_t next_ = 0;
    std::vector<std::uint32_t> free_;
    std::vector<bool> inUse_;
};

} // namespace dwe::rhi
