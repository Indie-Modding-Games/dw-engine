#pragma once

#include <string_view>

// The YAML keys of game/save's documents.
namespace dwe::game::save {

// The keys of a save file, in the order they are written. `complete` goes last so that a file cut
// short anywhere lacks it.
namespace save_key {
inline constexpr std::string_view Format = "format";
inline constexpr std::string_view FormatVersion = "format_version";
inline constexpr std::string_view GameVersion = "game_version";
inline constexpr std::string_view Ruleset = "ruleset";
inline constexpr std::string_view Label = "label";
inline constexpr std::string_view Summary = "summary";
inline constexpr std::string_view Created = "created";
inline constexpr std::string_view Updated = "updated";
inline constexpr std::string_view PlaySeconds = "play_seconds";
inline constexpr std::string_view AutosaveReasons = "autosave_reasons";
inline constexpr std::string_view Sections = "sections";
inline constexpr std::string_view Complete = "complete";
inline constexpr std::string_view Version = "version";
inline constexpr std::string_view State = "state";
} // namespace save_key

// The keys of a save's summary, in the order they are written.
namespace summary_key {
inline constexpr std::string_view Slot = "slot";
inline constexpr std::string_view Tamer = "tamer";
inline constexpr std::string_view Species = "species";
inline constexpr std::string_view Nickname = "nickname";
inline constexpr std::string_view Map = "map";
} // namespace summary_key

// The keys of the save settings.
namespace settings_key {
inline constexpr std::string_view ManualSlots = "manual_slots";
// From when autosaves had slots of their own: read and ignored.
inline constexpr std::string_view OldAutosaveSlots = "autosave_slots";
} // namespace settings_key

// The keys of the ruleset's save section.
namespace section_key {
inline constexpr std::string_view Autosave = "autosave";
inline constexpr std::string_view OnMapChange = "on_map_change";
inline constexpr std::string_view OnBattleEnd = "on_battle_end";
inline constexpr std::string_view OnPartnerSleep = "on_partner_sleep";
inline constexpr std::string_view OnNewDay = "on_new_day";
inline constexpr std::string_view OnEvolution = "on_evolution";
inline constexpr std::string_view OnTournamentResult = "on_tournament_result";
inline constexpr std::string_view OnRecruitJoin = "on_recruit_join";
inline constexpr std::string_view OnKeyItem = "on_key_item";
inline constexpr std::string_view OnDangerousArea = "on_dangerous_area";
inline constexpr std::string_view OnStoryProgress = "on_story_progress";
inline constexpr std::string_view OnPartnerReborn = "on_partner_reborn";
inline constexpr std::string_view CoalesceSeconds = "coalesce_seconds";
} // namespace section_key

// The names of the autosave reasons, as save headers list them.
namespace reason_name {
inline constexpr std::string_view MapChange = "map_change";
inline constexpr std::string_view BattleEnd = "battle_end";
inline constexpr std::string_view PartnerSleep = "partner_sleep";
inline constexpr std::string_view NewDay = "new_day";
inline constexpr std::string_view Evolution = "evolution";
inline constexpr std::string_view TournamentResult = "tournament_result";
inline constexpr std::string_view RecruitJoin = "recruit_join";
inline constexpr std::string_view KeyItem = "key_item";
inline constexpr std::string_view DangerousArea = "dangerous_area";
inline constexpr std::string_view StoryProgress = "story_progress";
inline constexpr std::string_view PartnerReborn = "partner_reborn";
} // namespace reason_name

} // namespace dwe::game::save
