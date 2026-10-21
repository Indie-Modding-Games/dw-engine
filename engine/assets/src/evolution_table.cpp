#include <dwe/assets/evolution_table.hpp>
#include <dwe/assets/messages.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>

#include <utility>

#include "game_table_document.hpp"

namespace dwe::assets {

namespace {

using core::YamlNode;

EvolutionRequirementEntry readRequirement(YamlReader& reader, const YamlNode& node) {
    if (!reader.expectKeys(node, messages::RequirementText,
                           {"target", "index", "digimon", "hp", "mp", "offense", "defense", "speed", "brain", "care",
                            "weight", "discipline", "happiness", "battles", "techs", "flags"})) {
        return {};
    }
    const auto integer = [&](std::string_view key) { return reader.integer<std::int32_t>(reader.field(node, key)); };
    return {.target = reader.optionalText(reader.field(node, "target")),
            .index = integer("index"),
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
            {"target", "index", "hp", "mp", "offense", "defense", "speed", "brains", "target_digimon"})) {
        return {};
    }
    const auto integer = [&](std::string_view key) { return reader.integer<std::int32_t>(reader.field(node, key)); };
    return {.target = reader.optionalText(reader.field(node, "target")),
            .index = integer("index"),
            .hp = integer("hp"),
            .mp = integer("mp"),
            .offense = integer("offense"),
            .defense = integer("defense"),
            .speed = integer("speed"),
            .brains = integer("brains"),
            .targetDigimon = reader.optionalText(reader.field(node, "target_digimon"))};
}

template <std::size_t Count>
void readReferences(YamlReader& reader, const YamlNode& node, std::string_view what,
                    std::array<TableReference, Count>& references) {
    const auto items = reader.sequence(node, what, Count);
    for (std::size_t i = 0; i < items.size(); ++i) {
        references.at(i) = reader.optionalText(items[i]);
    }
}

EvolutionPathEntry readPath(YamlReader& reader, const YamlNode& node) {
    if (!reader.expectKeys(node, messages::PathText, {"digimon", "index", "from", "to"})) {
        return {};
    }
    EvolutionPathEntry path{.digimon = reader.optionalText(reader.field(node, "digimon")),
                            .index = reader.integer<std::int32_t>(reader.field(node, "index")),
                            .from = {},
                            .to = {}};
    readReferences(reader, reader.field(node, "from"), "from", path.from);
    readReferences(reader, reader.field(node, "to"), "to", path.to);
    return path;
}

EvolutionItemTargetEntry readItemTarget(YamlReader& reader, const YamlNode& node) {
    if (!reader.expectKeys(node, messages::ItemTargetText, {"item", "index", "target"})) {
        return {};
    }
    return {.item = reader.optionalText(reader.field(node, "item")),
            .index = reader.integer<std::int32_t>(reader.field(node, "index")),
            .target = reader.optionalText(reader.field(node, "target"))};
}

core::Result<EvolutionTable> readEvolutionTable(const YamlNode& root) {
    YamlReader reader;
    if (!reader.expectKeys(root, messages::EvolutionTableText, {"requirements", "stats_gains", "paths"},
                           {"item_targets"})) {
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
    auto tree = readTableDocument(vfs, id, messages::EvolutionTableText);
    if (!tree) {
        return std::unexpected{tree.error()};
    }
    return readEvolutionTable(*tree);
}

} // namespace dwe::assets
