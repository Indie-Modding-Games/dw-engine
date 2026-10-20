#include <dwe/game/save/settings.hpp>
#include <dwe/session/slot_names.hpp>

#include <charconv>
#include <cstdint>
#include <system_error>

namespace dwe::session {

namespace {

constexpr char IndexSeparator = '_';

std::optional<game::save::SlotKind> kindOf(std::string_view prefix) {
    if (prefix == game::save::ManualSlotPrefix) {
        return game::save::SlotKind::Manual;
    }
    if (prefix == game::save::AutosaveSlotPrefix) {
        return game::save::SlotKind::Autosave;
    }
    return std::nullopt;
}

} // namespace

std::optional<game::save::SlotId> slotFromName(std::string_view name) {
    if (name.ends_with(game::save::SlotFileExtension)) {
        name.remove_suffix(game::save::SlotFileExtension.size());
    }
    const std::size_t separator = name.rfind(IndexSeparator);
    if (separator == std::string_view::npos) {
        return std::nullopt;
    }
    const std::optional<game::save::SlotKind> kind = kindOf(name.substr(0, separator));
    const std::string_view digits = name.substr(separator + 1);
    std::int32_t index = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), index);
    if (!kind || digits.empty() || error != std::errc{} || end != digits.data() + digits.size() || index < 1 ||
        index > game::save::MaxSlots) {
        return std::nullopt;
    }
    return game::save::SlotId{.kind = *kind, .index = index};
}

} // namespace dwe::session
