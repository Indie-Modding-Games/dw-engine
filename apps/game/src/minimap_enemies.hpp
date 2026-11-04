#pragma once

#include <dwe/assets/map_document.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/math.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <span>
#include <string_view>
#include <vector>

namespace dwe::app {

// Whether the minimap marks the enemies on the map (an engine improvement; the original has no
// minimap, so it changes no rule): a UI setting and the command with the same name that the game
// options' row sends, saved with the user's settings (minimap_enemies: true, false), on when not
// chosen yet. Off leaves the minimap with the player and the partner only.
inline constexpr std::string_view MinimapEnemiesSetting = "minimap_enemies";

// The sections of a script listing (script/dg/<n>, as dwx lists the original's script) that can
// start a battle: from the section's first instruction, following every jump, branch, choice,
// switch and call of the listing, a start_battle can be reached. The original has no flag for a
// wild NPC: an NPC fights because its section, which has its script id and runs when the player
// touches or talks to it (tamer.c: checkMapInteraction), reaches SCRIPT_OP_START_BATTLE, which
// fights the entity talked to (script_interp.c, tamer.c: startBattle). Its follow mode does not
// tell: many wild ones only turn to the player (2) and some that chase (12) only talk.
[[nodiscard]] std::set<std::int32_t> fightingSections(const core::YamlNode& listing);

// Where a map NPC stands on the field by its script id, or nothing when it is not on the map (not
// put there by the story, unloaded, or beaten and gone).
using NpcPosition = std::function<std::optional<glm::vec3>(std::int32_t scriptId)>;

// The enemies the minimap shows: none while `shown` (minimap_enemies) is off; otherwise the map's
// NPCs on the field whose section can start a battle, once per script id (a map can hold several
// records with one, and only the one on the map is the character), in the map's order. A story
// boss the player can fight is one of them: the dot says that touching or talking to it may start
// a fight, as for a wild one.
[[nodiscard]] std::vector<ui::MinimapMarker> minimapEnemies(bool shown,
                                                            std::span<const assets::MapNpc> records,
                                                            const std::set<std::int32_t>& fighting,
                                                            const NpcPosition& positionOf);

// The fighting sections of the map's active script, read from its listing again only when the
// script changes. No script (the story does not run the map) or no listing: no section fights.
class MinimapEnemyFinder {
public:
  // The files must outlive the finder.
  explicit MinimapEnemyFinder(const assets::VirtualFileSystem& files) : files_{&files} {}

  [[nodiscard]] const std::set<std::int32_t>& sectionsOf(std::optional<std::uint16_t> script);

private:
  const assets::VirtualFileSystem* files_;
  std::optional<std::optional<std::uint16_t>> read_;
  std::set<std::int32_t> sections_;
};

} // namespace dwe::app
