#include "coop_npc_presence.hpp"

#include <dwe/core/log.hpp>

#include <algorithm>

namespace dwe::app {

namespace coop = game::coop;

namespace {

using Key = std::pair<coop::WorldEditKind, std::int32_t>;

// The facts as `now` with the changed ones `restored` takes back to the value they had on load.
template <typename Restored>
game::field::GuardFacts factsBefore(const game::field::GuardFacts& now, Restored restored) {
  game::field::GuardFacts before = now;
  if (now.triggers) {
    before.triggers = [restored, &now](std::int32_t trigger) -> std::optional<bool> {
      const auto value = restored(Key{coop::WorldEditKind::Trigger, trigger});
      return value ? std::optional{*value != 0} : now.triggers(trigger);
    };
  }
  if (now.variables) {
    before.variables = [restored, &now](std::int32_t variable) -> std::optional<std::int64_t> {
      const auto value = restored(Key{coop::WorldEditKind::Variable, variable});
      return value ? std::optional<std::int64_t>{*value} : now.variables(variable);
    };
  }
  return before;
}

} // namespace

void CoopNpcPresence::note(std::uint32_t mapSerial, std::span<const CoopFactChange> changes,
                           Origin origin) {
  if (changes.empty()) {
    return;
  }
  if (serial_ != mapSerial) {
    previous_.clear();
    serial_ = mapSerial;
  }
  for (const CoopFactChange& change : changes) {
    // The first value is the one the map head decided with.
    const auto [entry, added] =
        previous_.try_emplace({change.edit.kind, change.edit.key},
                              Before{.value = change.previous, .byOther = origin == Origin::Other});
    entry->second.byOther = entry->second.byOther || origin == Origin::Other;
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
  game::field::FieldNpcs& npcs = *stage.npcs;
  // Leaving follows only the other players' changes; arriving follows every change.
  const auto byOthers = [&previous](const Key& key) -> std::optional<std::int32_t> {
    const auto found = previous.find(key);
    return found != previous.end() && found->second.byOther ? std::optional{found->second.value}
                                                            : std::nullopt;
  };
  const auto byAnyone = [&previous](const Key& key) -> std::optional<std::int32_t> {
    const auto found = previous.find(key);
    return found != previous.end() ? std::optional{found->second.value} : std::nullopt;
  };
  const game::field::NpcSlotOccupied occupied = [&npcs](std::size_t slot) {
    return npcs.entityAt(slot).has_value();
  };
  const std::vector<std::size_t> leave =
      std::ranges::any_of(previous, [](const auto& entry) { return entry.second.byOther; })
          ? game::field::npcPresenceChange(rules, map, factsBefore(now, byOthers), now, occupied)
                .leave
          : std::vector<std::size_t>{};
  const game::field::NpcPresenceChange change =
      game::field::npcPresenceChange(rules, map, factsBefore(now, byAnyone), now);
  std::size_t moved = 0;
  for (const std::size_t slot : leave) {
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
      core::logInfo("coop: a story change brings NPC slot {} (species {}) to {} ({})", spawn->slot,
                    spawn->species, map, spawn->source);
      ++moved;
    }
  }
  return moved;
}

} // namespace dwe::app
