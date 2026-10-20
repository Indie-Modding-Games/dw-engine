#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace dwe::render {

// Where a value lives in a SlotMap: its slot and that slot's generation, which tells a live value from an earlier one
// that used the same slot.
struct SlotKey {
    static constexpr std::uint32_t InvalidIndex = std::numeric_limits<std::uint32_t>::max();

    std::uint32_t index = InvalidIndex;
    std::uint32_t generation = 0;

    [[nodiscard]] bool valid() const { return index != InvalidIndex; }
    friend bool operator==(const SlotKey&, const SlotKey&) = default;
};

// Values reached by generational keys: erasing a value bumps its slot's generation, so a key kept after the erase,
// even once the slot holds a new value, finds nothing. Plain CPU bookkeeping, tested without a GPU.
template <typename T>
class SlotMap {
public:
    SlotKey insert(T value) {
        if (!free_.empty()) {
            const std::uint32_t index = free_.back();
            free_.pop_back();
            slots_[index].value.emplace(std::move(value));
            return {.index = index, .generation = slots_[index].generation};
        }
        slots_.push_back({.value = std::move(value), .generation = 0});
        return {.index = static_cast<std::uint32_t>(slots_.size() - 1), .generation = 0};
    }

    // The value of a key, or nullptr if it was never inserted or was erased.
    [[nodiscard]] T* find(SlotKey key) {
        if (key.index >= slots_.size()) {
            return nullptr;
        }
        Slot& slot = slots_[key.index];
        return slot.generation == key.generation && slot.value.has_value() ? &*slot.value : nullptr;
    }
    [[nodiscard]] const T* find(SlotKey key) const {
        if (key.index >= slots_.size()) {
            return nullptr;
        }
        const Slot& slot = slots_[key.index];
        return slot.generation == key.generation && slot.value.has_value() ? &*slot.value : nullptr;
    }

    // Takes the value of a key out, or nothing for a stale key.
    std::optional<T> erase(SlotKey key) {
        if (find(key) == nullptr) {
            return std::nullopt;
        }
        Slot& slot = slots_[key.index];
        std::optional<T> out = std::move(slot.value);
        slot.value.reset();
        ++slot.generation;
        free_.push_back(key.index);
        return out;
    }

    // Calls f(key, value) for every live value.
    template <typename F>
    void forEach(const F& f) {
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            Slot& slot = slots_[i];
            if (slot.value.has_value()) {
                f(SlotKey{.index = static_cast<std::uint32_t>(i), .generation = slot.generation}, *slot.value);
            }
        }
    }

private:
    struct Slot {
        std::optional<T> value;
        std::uint32_t generation = 0;
    };

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_;
};

} // namespace dwe::render
