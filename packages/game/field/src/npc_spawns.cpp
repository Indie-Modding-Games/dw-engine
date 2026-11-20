#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/field/npc_spawns.hpp>

#include <algorithm>
#include <format>
#include <set>
#include <utility>

#include "guard_yaml.hpp"

namespace dwe::game::field {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view NpcSpawnsId = "field/npc_spawns";
constexpr std::string_view Context = "npc spawns";

NpcSpawnRule readSpawn(YamlReader& reader, const YamlNode& item) {
  NpcSpawnRule rule;
  if (!reader.expectKeys(item, "an NPC spawn", {"map", "slot", "species", "autotalk", "source"},
                         {"guard", "section_writes"})) {
    return rule;
  }
  rule.map = reader.text(reader.field(item, "map"));
  rule.slot = reader.integer<std::size_t>(reader.field(item, "slot"));
  rule.species = reader.integer<std::int32_t>(reader.field(item, "species"));
  rule.autotalk = reader.boolean(reader.field(item, "autotalk"));
  rule.guard = readGuard(reader, item.find("guard"));
  if (const YamlNode* written = item.find("section_writes"); written != nullptr) {
    for (const YamlNode& variable : reader.sequence(*written, "section_writes")) {
      rule.sectionWrites.push_back(reader.integer<std::int32_t>(variable));
    }
  }
  const YamlNode& source = reader.field(item, "source");
  if (reader.expectKeys(source, "the source", {"script", "section"}, {"offset"})) {
    rule.source = std::format("script {}, section {}",
                              reader.integer<std::int32_t>(reader.field(source, "script")),
                              reader.integer<std::int32_t>(reader.field(source, "section")));
    if (const auto offset = reader.optionalInteger<std::int32_t>(source, "offset")) {
      rule.source += std::format(", offset 0x{:04x}", *offset);
    }
  }
  return rule;
}

// Whether a spawn's guard holds; a variable the section writes itself reads as unknown.
Truth spawnHolds(const NpcSpawnRule& rule, const GuardFacts& facts) {
  if (rule.sectionWrites.empty() || !facts.variables) {
    return evaluate(rule.guard, facts);
  }
  GuardFacts hidden = facts;
  hidden.variables = [&rule, &facts](std::int32_t variable) -> std::optional<std::int64_t> {
    if (std::ranges::contains(rule.sectionWrites, variable)) {
      return std::nullopt;
    }
    return facts.variables(variable);
  };
  return evaluate(rule.guard, hidden);
}

// The first of the slot's spawns that holds; nothing when none does.
const NpcSpawnRule* holdingSpawn(std::span<const NpcSpawnRule> rules, std::string_view map,
                                 std::size_t slot, const GuardFacts& facts) {
  const auto found = std::ranges::find_if(rules, [&](const NpcSpawnRule& rule) {
    return rule.map == map && rule.slot == slot && spawnHolds(rule, facts) == Truth::True;
  });
  return found != rules.end() ? &*found : nullptr;
}

} // namespace

core::Result<std::vector<NpcSpawnRule>> parseNpcSpawns(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{core::Error{std::format("{}: {}", Context, tree.error().message)}};
  }
  YamlReader reader;
  if (!reader.expectKeys(*tree, "the NPC spawns", {"spawns"})) {
    return std::unexpected{reader.error(Context)};
  }
  std::vector<NpcSpawnRule> rules;
  for (const YamlNode& item : reader.sequence(reader.field(*tree, "spawns"), "spawns")) {
    rules.push_back(readSpawn(reader, item));
    if (!reader.ok()) {
      break;
    }
  }
  return reader.finish(std::move(rules), Context);
}

core::Result<std::vector<NpcSpawnRule>> loadNpcSpawns(const assets::VirtualFileSystem& vfs) {
  auto id = assets::AssetId::parse(NpcSpawnsId);
  if (!id) {
    return std::unexpected{id.error()};
  }
  if (vfs.resolve(*id) == nullptr) {
    return std::vector<NpcSpawnRule>{};
  }
  auto bytes = vfs.read(*id);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  return parseNpcSpawns(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
}

Truth spawnsInSlot(std::span<const NpcSpawnRule> rules, std::string_view map, std::size_t slot,
                   const GuardFacts& facts) {
  Truth result = Truth::False;
  for (const NpcSpawnRule& rule : rules) {
    if (rule.map != map || rule.slot != slot) {
      continue;
    }
    const Truth value = spawnHolds(rule, facts);
    if (value == Truth::True) {
      return Truth::True;
    }
    if (value == Truth::Unknown) {
      result = Truth::Unknown;
    }
  }
  return result;
}

NpcPresenceChange npcPresenceChange(std::span<const NpcSpawnRule> rules, std::string_view map,
                                    const GuardFacts& before, const GuardFacts& after) {
  NpcPresenceChange change;
  std::set<std::size_t> slots;
  for (const NpcSpawnRule& rule : rules) {
    if (rule.map == map) {
      slots.insert(rule.slot);
    }
  }
  for (const std::size_t slot : slots) {
    const Truth was = spawnsInSlot(rules, map, slot, before);
    const Truth now = spawnsInSlot(rules, map, slot, after);
    if (was == Truth::True && now == Truth::False) {
      change.leave.push_back(slot);
    } else if (was == Truth::False && now == Truth::True) {
      change.arrive.push_back(holdingSpawn(rules, map, slot, after));
    }
  }
  return change;
}

} // namespace dwe::game::field
