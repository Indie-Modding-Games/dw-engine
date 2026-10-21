#pragma once

#include <dwe/game/inventory/inventory.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// The bag slot a UI command names, shared by the screens that show the bag (partner menu, battle).
namespace dwe::app {

// A slot number of a command, or nothing for one that is not a whole number.
inline std::optional<std::size_t> slotOf(double value) {
    if (!std::isfinite(value) || value < 0.0 || std::trunc(value) != value) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(value);
}

// The item in a slot of the bag, or nothing for an empty slot or one past the bag.
inline std::optional<std::int32_t> itemAt(std::span<const std::optional<game::inventory::Stack>> slots,
                                          std::optional<std::size_t> slot) {
    if (!slot || *slot >= slots.size()) {
        return std::nullopt;
    }
    const std::optional<game::inventory::Stack>& stack = slots[*slot];
    if (!stack) {
        return std::nullopt;
    }
    return stack->item;
}

} // namespace dwe::app
