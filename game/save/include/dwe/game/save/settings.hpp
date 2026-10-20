#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <string_view>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::game::save {

// The asset the save settings are read from.
inline constexpr std::string_view SettingsAssetId = "save/settings";

// The most slots of each kind: slot file names have two digits.
inline constexpr std::int32_t MaxSlots = 99;

// How many save slots there are, read from `save/settings` in the game pack
// (content/schemas/save_settings.schema.json). When the game autosaves is the ruleset's (AutosaveRules).
struct SaveSettings {
    // Slots the player saves to from the save menu. The original had up to 15 files per memory card, one per slot
    // (main_menu.c: SAVEGAME_SLOT_INFO).
    std::int32_t manualSlots = 15;
    // Slots the autosave writes to in turn, overwriting the oldest; 0 turns the autosave off.
    std::int32_t autosaveSlots = 0;
};

// Reads the save settings.
core::Result<SaveSettings> parseSaveSettings(std::string_view yaml);

// Reads the save settings from the mounted packs.
core::Result<SaveSettings> loadSaveSettings(const assets::VirtualFileSystem& vfs);

} // namespace dwe::game::save
