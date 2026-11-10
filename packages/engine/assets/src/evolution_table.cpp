#include <dwe/assets/evolution_table.hpp>
#include <dwe/assets/messages.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>

#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

#include "game_table_document.hpp"

namespace dwe::assets {

namespace {

using core::YamlNode;

EvolutionRequirementEntry readRequirement(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, messages::RequirementText,
                         {"target", "digimon", "hp", "mp", "offense", "defense", "speed", "brain",
                          "care", "weight", "discipline", "happiness", "battles", "techs", "flags"},
                         {EntryIndexKey})) {
    return {};
  }
  const auto integer = [&](std::string_view key) {
    return reader.integer<std::int32_t>(reader.field(node, key));
  };
  return {.target = reader.optionalText(reader.field(node, "target")),
          .index = reader.optionalInteger<std::int32_t>(node, EntryIndexKey),
          .digimon = reader.optionalText(reader.field(node, "digimon")),
          .hp = integer("hp"),
          .mp = integer("mp"),
          .offense = integer("offense"),
          .defense = integer("defense"),
          .speed = integer("speed"),
          .brain = integer("brain"),
          .care = integer("care"),
          .weight = integer("weight"),
          .discipline = integer("discipline"),
          .happiness = integer("happiness"),
          .battles = integer("battles"),
          .techs = integer("techs"),
          .flags = integer("flags")};
}

EvolutionStatsGainEntry readStatsGain(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(
          node, messages::StatsGainText,
          {"target", "hp", "mp", "offense", "defense", "speed", "brains", "target_digimon"},
          {EntryIndexKey})) {
    return {};
  }
  const auto integer = [&](std::string_view key) {
    return reader.integer<std::int32_t>(reader.field(node, key));
  };
  return {.target = reader.optionalText(reader.field(node, "target")),
          .index = reader.optionalInteger<std::int32_t>(node, EntryIndexKey),
          .hp = integer("hp"),
          .mp = integer("mp"),
          .offense = integer("offense"),
          .defense = integer("defense"),
          .speed = integer("speed"),
          .brains = integer("brains"),
          .targetDigimon = reader.optionalText(reader.field(node, "target_digimon"))};
}

void readReferences(YamlReader& reader, const YamlNode& node, std::string_view what,
                    std::size_t count, std::vector<TableReference>& references) {
  const auto items = reader.sequence(node, what, count);
  references.assign(count, std::nullopt);
  for (std::size_t i = 0; i < items.size() && i < count; ++i) {
    references.at(i) = reader.optionalText(items[i]);
  }
}

EvolutionPathEntry readPath(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, messages::PathText, {"digimon", "from", "to"}, {EntryIndexKey})) {
    return {};
  }
  EvolutionPathEntry path{.digimon = reader.optionalText(reader.field(node, "digimon")),
                          .index = reader.optionalInteger<std::int32_t>(node, EntryIndexKey)};
  readReferences(reader, reader.field(node, "from"), "from", EvolutionPathSources, path.from);
  readReferences(reader, reader.field(node, "to"), "to", EvolutionPathTargets, path.to);
  return path;
}

EvolutionItemTargetEntry readItemTarget(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, messages::ItemTargetText, {"item", "target"}, {EntryIndexKey})) {
    return {};
  }
  return {.item = reader.optionalText(reader.field(node, "item")),
          .index = reader.optionalInteger<std::int32_t>(node, EntryIndexKey),
          .target = reader.optionalText(reader.field(node, "target"))};
}

core::Result<EvolutionTable> readEvolutionTable(const YamlNode& root) {
  YamlReader reader;
  if (!reader.expectKeys(root, messages::EvolutionTableText,
                         {"requirements", "stats_gains", "paths"}, {"item_targets"})) {
    return std::unexpected{reader.error(messages::EvolutionTableText)};
  }
  EvolutionTable table;
  for (const YamlNode& item : reader.sequence(reader.field(root, "requirements"), "requirements")) {
    table.requirements.push_back(readRequirement(reader, item));
  }
  for (const YamlNode& item : reader.sequence(reader.field(root, "stats_gains"), "stats_gains")) {
    table.statsGains.push_back(readStatsGain(reader, item));
  }
  for (const YamlNode& item : reader.sequence(reader.field(root, "paths"), "paths")) {
    table.paths.push_back(readPath(reader, item));
  }
  if (const YamlNode* targets = root.find("item_targets"); targets != nullptr) {
    for (const YamlNode& item : reader.sequence(*targets, "item_targets")) {
      table.itemTargets.push_back(readItemTarget(reader, item));
    }
  }
  return reader.finish(std::move(table), messages::EvolutionTableText);
}

} // namespace

core::Result<EvolutionTable> parseEvolutionTable(std::string_view yaml) {
  auto tree = parseTableDocument(yaml, messages::EvolutionTableText);
  if (!tree) {
    return std::unexpected{tree.error()};
  }
  return readEvolutionTable(*tree);
}

core::Result<EvolutionTable> loadEvolutionTable(const VirtualFileSystem& vfs, const AssetId& id) {
  auto tree = readTableDocument(vfs, id, messages::EvolutionTableText, EvolutionComposition);
  if (!tree) {
    return std::unexpected{tree.error()};
  }
  return readEvolutionTable(*tree);
}

} // namespace dwe::assets
