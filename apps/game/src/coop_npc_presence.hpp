#pragma once

#include <dwe/game/field/npc_spawns.hpp>
#include <dwe/game/field/npcs.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "coop_world_state.hpp"

namespace dwe::app {

// What story changes do to the NPCs this game's map head put on its map, shown at once in co-op
// (#357, docs/architecture/multiplayer.md «World changes shown at once»). The original shows a
// recruit leaving its map in the recruit's own scene (unload_entity) and a shop's keeper or a
// recruit's helper only on the next load of its map, as the MAPHEAD section decides on entering
// (set_digimon under its guards). Here, once a shared fact changed, the slots whose spawn decision
// it flipped follow it: an NPC the map head would no longer put there leaves, one it now would
// arrives. Nothing runs the map head again: field/npc_spawns holds each spawn's guard.
// - Another player's change: NPCs come and go. A slot whose decision on load is not known (a count
//   or a random roll the section keeps, as File City's visitors) still loses its NPC when no way
//   through the section places it any more.
// - This game's own change: only arrivals, once its scene is over (the scene already took away
//   what it meant to; what it leaves standing stays until the next load, as in the original). So
//   the player who opened a shop or recruited a helper sees the newcomer as the others do.
// Nothing happens while a script of this game runs or its map changes.
class CoopNpcPresence {
public:
  enum class Origin : std::uint8_t { Other, Own };

  // The changes this game took while standing on map load `mapSerial` (none dropped: a change that
  // goes back to the value it had flips nothing).
  void note(std::uint32_t mapSerial, std::span<const CoopFactChange> changes,
            Origin origin = Origin::Other);
  [[nodiscard]] bool pending() const { return !previous_.empty(); }

  // Where the NPCs go: the map's NPC slots, and how a spawn puts one there (the story's
  // spawn_npc, which gives the model and size).
  struct Stage {
    game::field::FieldNpcs* npcs = nullptr;
    std::function<bool(const game::field::NpcSpawnRule& spawn)> spawn;
  };
  // Once the map stands still (`ready`: settled, no script running) on the load the changes came
  // on, the NPCs follow them; a new map load drops them (its map head decided with them). `now`
  // reads the story as it is; the changes give what it was. Returns how many NPCs came or went.
  std::size_t follow(std::span<const game::field::NpcSpawnRule> rules, std::string_view map,
                     std::uint32_t mapSerial, bool ready, const game::field::GuardFacts& now,
                     const Stage& stage);

private:
  // The value a changed fact had before its first change on this load, and whether another
  // player changed it.
  struct Before {
    std::int32_t value = 0;
    bool byOther = false;
  };
  std::optional<std::uint32_t> serial_;
  std::map<std::pair<game::coop::WorldEditKind, std::int32_t>, Before> previous_;
};

} // namespace dwe::app
