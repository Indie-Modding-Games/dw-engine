#include <dwe/assets/messages.hpp>
#include <dwe/assets/move_table.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <span>
#include <utility>

#include "game_table_document.hpp"

namespace dwe::assets {

namespace {

using core::YamlNode;

constexpr std::string_view LearnChancesKey = "learn_chances";

std::optional<LearnChances> readLearnChances(YamlReader& reader, const YamlNode& move) {
    const YamlNode* node = move.find(LearnChancesKey);
    if (node == nullptr) {
        return std::nullopt;
    }
    LearnChances chances{};
    const std::span<const YamlNode> items = reader.sequence(*node, messages::LearnChancesText, chances.size());
    for (std::size_t slot = 0; slot < items.size(); ++slot) {
        chances.at(slot) = reader.integer<std::int32_t>(items[slot]);
    }
    return chances;
}

MoveEntry readMove(YamlReader& reader, const YamlNode& node) {
    if (!reader.expectKeys(node, messages::MoveText,
                           {"id", "index", "name", "distance", "power", "mp_cost", "iframes", "range", "special",
                            "status", "accuracy", "status_chance", "unknown_0d", "unknown_0e", "unknown_0f"},
                           {LearnChancesKey})) {
        return {};
    }
    const auto integer = [&](std::string_view key) { return reader.integer<std::int32_t>(reader.field(node, key)); };
    return {.id = reader.text(reader.field(node, "id")),
            .index = integer("index"),
            .name = reader.optionalText(reader.field(node, "name")),
            .distance = integer("distance"),
            .power = integer("power"),
            .mpCost = integer("mp_cost"),
            .iframes = integer("iframes"),
            .range = integer("range"),
            .special = integer("special"),
            .status = integer("status"),
            .accuracy = integer("accuracy"),
            .statusChance = integer("status_chance"),
            .flags = integer("unknown_0d"),
            .unknown0e = integer("unknown_0e"),
            .unknown0f = integer("unknown_0f"),
            .learnChances = readLearnChances(reader, node)};
}

core::Result<MoveTable> readMoveTable(const YamlNode& root) {
    YamlReader reader;
    if (!reader.expectKeys(root, messages::MoveTableText, {"moves"})) {
        return std::unexpected{reader.error(messages::MoveTableText)};
    }
    MoveTable table;
    for (const YamlNode& item : reader.sequence(reader.field(root, "moves"), "moves")) {
        table.moves.push_back(readMove(reader, item));
        const MoveEntry& added = table.moves.back();
        if (reader.ok() && table.find(added.id) != &added) {
            reader.fail(item, messages::formatMessage(messages::RepeatedEntry, messages::MoveText, added.id));
        } else if (reader.ok() && table.findByIndex(added.index) != &added) {
            reader.fail(item,
                        messages::formatMessage(messages::RepeatedIndex, messages::MoveText, added.id, added.index));
        }
    }
    return reader.finish(std::move(table), messages::MoveTableText);
}

} // namespace

const MoveEntry* MoveTable::find(std::string_view id) const {
    const auto found = std::ranges::find(moves, id, &MoveEntry::id);
    return found == moves.end() ? nullptr : &*found;
}

const MoveEntry* MoveTable::findByIndex(std::int32_t index) const {
    const auto found = std::ranges::find(moves, index, &MoveEntry::index);
    return found == moves.end() ? nullptr : &*found;
}

core::Result<MoveTable> parseMoveTable(std::string_view yaml) {
    auto tree = parseTableDocument(yaml, messages::MoveTableText);
    if (!tree) {
        return std::unexpected{tree.error()};
    }
    return readMoveTable(*tree);
}

core::Result<MoveTable> loadMoveTable(const VirtualFileSystem& vfs, const AssetId& id) {
    auto tree = readTableDocument(vfs, id, messages::MoveTableText);
    if (!tree) {
        return std::unexpected{tree.error()};
    }
    return readMoveTable(*tree);
}

} // namespace dwe::assets
