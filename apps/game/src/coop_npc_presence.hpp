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

// What other players' story changes do to the NPCs this game's map head put on its map, shown at
// once (#357, docs/architecture/multiplayer.md «World changes shown at once»). The original shows
// a recruit leaving its map in the recruit's own scene (unload_entity) and a shop's keeper only
// on the next load of its map, as the MAPHEAD section decides on entering (set_digimon under its
// guards); here, when a shared fact another player changed arrives, the slots whose spawn decision
// it flipped follow it: an NPC the map head would no longer put there leaves, one it now would
// arrives. Nothing runs the map head again: field/npc_spawns holds each spawn's guard. This game's
// own changes keep the original's behaviour (its scene already showed them), and nothing happens
// while a script of this game runs or its map changes.
class CoopNpcPresence {
public:
  // The changes this game took while standing on map load `mapSerial` (none dropped: a change that
  // goes back to the value it had flips nothing).
  void note(std::uint32_t mapSerial, std::span<const CoopFactChange> changes);
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
  std::optional<std::uint32_t> serial_;
  // The value each changed fact had before the first change of it, by kind and key.
  std::map<std::pair<game::coop::WorldEditKind, std::int32_t>, std::int32_t> previous_;
};

} // namespace dwe::app
