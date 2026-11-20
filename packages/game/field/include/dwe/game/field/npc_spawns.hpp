#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/field/script_warps.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

// The NPCs the MAPHEAD section of each map puts on it as the map is entered (`field/npc_spawns`,
// which dwx extracts; content/schemas/npc_spawns.schema.json), each with the guard of its path: a
// recruit's NPC stands on its map while its trigger is clear, a shop's keeper once the shop opened.
// The map reads them only on its next load; co-op reads them to show at once what another player's
// story change brings or takes away (docs/architecture/multiplayer.md, «World changes shown at
// once»).
namespace dwe::game::field {

struct NpcSpawnRule {
  std::string map;
  std::size_t slot = 0; // the map's NPC record
  std::int32_t species = 0;
  bool autotalk = false;
  WarpGuard guard;
  // Variables the section writes itself before testing them: the guard's tests of them are unknown.
  std::vector<std::int32_t> sectionWrites;
  std::string source; // "script 0, section 1, offset 0x04cc"

  bool operator==(const NpcSpawnRule&) const = default;
};

// Parses the text of `field/npc_spawns`. Unknown and missing keys are errors.
core::Result<std::vector<NpcSpawnRule>> parseNpcSpawns(std::string_view yaml);

// Reads `field/npc_spawns` from the virtual file system; nothing when no pack has it (content
// extracted before it existed).
core::Result<std::vector<NpcSpawnRule>> loadNpcSpawns(const assets::VirtualFileSystem& vfs);

// Whether a map's section puts an NPC in a slot: True when one of the slot's spawns holds, False
// when none can, Unknown otherwise (a test of something the facts do not know).
Truth spawnsInSlot(std::span<const NpcSpawnRule> rules, std::string_view map, std::size_t slot,
                   const GuardFacts& facts);

// What a change of the facts does to the NPCs a map's section puts on it: the slots it put an NPC
// in before and no longer would, and the spawns it now makes in slots it left empty before. A slot
// whose decision is Unknown either way is left alone.
struct NpcPresenceChange {
  std::vector<std::size_t> leave;
  std::vector<const NpcSpawnRule*> arrive;

  [[nodiscard]] bool empty() const { return leave.empty() && arrive.empty(); }
};

NpcPresenceChange npcPresenceChange(std::span<const NpcSpawnRule> rules, std::string_view map,
                                    const GuardFacts& before, const GuardFacts& after);

} // namespace dwe::game::field
