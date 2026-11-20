#include "coop_npc_presence.hpp"

#include <dwe/core/log.hpp>

#include <algorithm>

namespace dwe::app {

namespace coop = game::coop;

void CoopNpcPresence::note(std::uint32_t mapSerial, std::span<const CoopFactChange> changes) {
  if (changes.empty()) {
    return;
  }
  if (serial_ != mapSerial) {
    previous_.clear();
    serial_ = mapSerial;
  }
  for (const CoopFactChange& change : changes) {
    // The first value is the one the map head decided with.
    previous_.try_emplace({change.edit.kind, change.edit.key}, change.previous);
  }
}

std::size_t CoopNpcPresence::follow(std::span<const game::field::NpcSpawnRule> rules,
                                    std::string_view map, std::uint32_t mapSerial, bool ready,
                                    const game::field::GuardFacts& now, const Stage& stage) {
  if (previous_.empty()) {
    return 0;
  }
  if (serial_ != mapSerial) {
    // Another map load: its map head read the changes itself.
    previous_.clear();
    return 0;
  }
  if (!ready || stage.npcs == nullptr) {
    return 0;
  }
  const auto previous = std::exchange(previous_, {});
  game::field::GuardFacts before = now;
  if (now.triggers) {
    before.triggers = [&previous, &now](std::int32_t trigger) -> std::optional<bool> {
      const auto found = previous.find({coop::WorldEditKind::Trigger, trigger});
      return found != previous.end() ? std::optional{found->second != 0} : now.triggers(trigger);
    };
  }
  if (now.variables) {
    before.variables = [&previous, &now](std::int32_t variable) -> std::optional<std::int64_t> {
      const auto found = previous.find({coop::WorldEditKind::Variable, variable});
      return found != previous.end() ? std::optional<std::int64_t>{found->second}
                                     : now.variables(variable);
    };
  }
  const game::field::NpcPresenceChange change =
      game::field::npcPresenceChange(rules, map, before, now);
  std::size_t moved = 0;
  game::field::FieldNpcs& npcs = *stage.npcs;
  for (const std::size_t slot : change.leave) {
    if (npcs.unloadAt(slot)) {
      core::logInfo("coop: another player's story change takes NPC slot {} off {}", slot, map);
      ++moved;
    }
  }
  for (const game::field::NpcSpawnRule* spawn : change.arrive) {
    if (spawn == nullptr || npcs.entityAt(spawn->slot) ||
        std::ranges::contains(npcs.goneUntilReload(), spawn->slot)) {
      continue;
    }
    if (stage.spawn && stage.spawn(*spawn)) {
      core::logInfo("coop: another player's story change brings NPC slot {} (species {}) to {} "
                    "({})",
                    spawn->slot, spawn->species, map, spawn->source);
      ++moved;
    }
  }
  return moved;
}

} // namespace dwe::app
