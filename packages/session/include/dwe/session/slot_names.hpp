#pragma once

#include <dwe/game/save/save_file.hpp>
#include <dwe/game/save/slots.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace dwe::session {

// The slot a name gives, as --list-saves writes them: `slot_01` or `autosave_02`, with or without
// `.yaml`. Nothing if it names no slot; whether the slot exists is up to the save settings.
std::optional<game::save::SlotId> slotFromName(std::string_view name);

// As slotFromName, only for a manual slot: nothing for an autosave slot or an empty name.
std::optional<game::save::SlotId> manualSlotFromName(std::string_view name);

// A slot's name as slotFromName reads it: `slot_01`, `autosave_02`.
std::string slotName(game::save::SlotId slot);

// The slot list's summary of a save, read from the session's own sections (the partner's species,
// the map): for a save written before the header had one (SaveSlots::setSummaryReader). What it
// cannot find stays empty; the slot is never known.
game::save::SaveSummary summaryFromSections(const game::save::SaveFile& file);

} // namespace dwe::session
