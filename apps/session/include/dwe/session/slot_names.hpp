#pragma once

#include <dwe/game/save/slots.hpp>

#include <optional>
#include <string_view>

namespace dwe::session {

// The slot a name gives, as --list-saves writes them: `slot_01` or `autosave_02`, with or without `.yaml`. Nothing if
// it names no slot; whether the slot exists is up to the save settings.
std::optional<game::save::SlotId> slotFromName(std::string_view name);

} // namespace dwe::session
