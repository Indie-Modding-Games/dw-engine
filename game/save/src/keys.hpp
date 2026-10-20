#pragma once

#include <string_view>

// The YAML keys of game/save's documents.
namespace dwe::game::save {

// The keys of a save file, in the order they are written. `complete` goes last so that a file cut short anywhere
// lacks it.
namespace save_key {
inline constexpr std::string_view Format = "format";
inline constexpr std::string_view FormatVersion = "format_version";
inline constexpr std::string_view GameVersion = "game_version";
inline constexpr std::string_view Ruleset = "ruleset";
inline constexpr std::string_view Label = "label";
inline constexpr std::string_view Created = "created";
inline constexpr std::string_view Updated = "updated";
inline constexpr std::string_view PlaySeconds = "play_seconds";
inline constexpr std::string_view Sections = "sections";
inline constexpr std::string_view Complete = "complete";
inline constexpr std::string_view Version = "version";
inline constexpr std::string_view State = "state";
} // namespace save_key

// The keys of the save settings.
namespace settings_key {
inline constexpr std::string_view ManualSlots = "manual_slots";
inline constexpr std::string_view AutosaveSlots = "autosave_slots";
} // namespace settings_key

// The keys of the ruleset's save section.
namespace section_key {
inline constexpr std::string_view Autosave = "autosave";
inline constexpr std::string_view OnMapChange = "on_map_change";
inline constexpr std::string_view OnBattleEnd = "on_battle_end";
inline constexpr std::string_view OnPartnerSleep = "on_partner_sleep";
inline constexpr std::string_view OnNewDay = "on_new_day";
} // namespace section_key

} // namespace dwe::game::save
