#include <dwe/assets/messages.hpp>
#include <dwe/assets/move_table.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/assert.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

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
  const std::span<const YamlNode> items =
      reader.sequence(*node, messages::LearnChancesText, chances.size());
  for (std::size_t slot = 0; slot < items.size(); ++slot) {
    chances.at(slot) = reader.integer<std::int32_t>(items[slot]);
  }
  return chances;
}

MoveEntry readMove(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, messages::MoveText,
                         {"id", EntryIndexKey, "name", "distance", "power", "mp_cost", "iframes",
                          "range", "special", "status", "accuracy", "status_chance", "unknown_0d",
                          "unknown_0e", "unknown_0f"},
                         {LearnChancesKey})) {
    return {};
  }
  const auto integer = [&](std::string_view key) {
    return reader.integer<std::int32_t>(reader.field(node, key));
  };
  return {.id = reader.text(reader.field(node, "id")),
          .index = integer(EntryIndexKey),
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

core::Result<MoveTable> readMoveTable(const YamlNode& root,
                                      const std::set<std::string, std::less<>>& engineNumbered) {
  YamlReader reader;
  if (!reader.expectKeys(root, messages::MoveTableText, {"moves"})) {
    return std::unexpected{reader.error(messages::MoveTableText)};
  }
  std::vector<MoveEntry> moves;
  for (const YamlNode& item : reader.sequence(reader.field(root, "moves"), "moves")) {
    MoveEntry added = readMove(reader, item);
    added.originalIndex = !engineNumbered.contains(added.id);
    if (reader.ok() && std::ranges::contains(moves, added.id, &MoveEntry::id)) {
      reader.fail(item,
                  messages::formatMessage(messages::RepeatedEntry, messages::MoveText, added.id));
    } else if (reader.ok() && std::ranges::contains(moves, added.index, &MoveEntry::index)) {
      reader.fail(item, messages::formatMessage(messages::RepeatedIndex, messages::MoveText,
                                                added.id, added.index));
    }
    moves.push_back(std::move(added));
  }
  auto read = reader.finish(std::move(moves), messages::MoveTableText);
  if (!read) {
    return std::unexpected{read.error()};
  }
  return MoveTable{std::move(*read)};
}

} // namespace

MoveTable::MoveTable() : generation_{CompositionGeneration::next()} {}

MoveTable::MoveTable(std::vector<MoveEntry> moves)
    : moves_{std::move(moves)}, generation_{CompositionGeneration::next()} {}

const MoveEntry* MoveTable::find(std::string_view id) const {
  const auto found = std::ranges::find(moves_, id, &MoveEntry::id);
  return found == moves_.end() ? nullptr : &*found;
}

std::optional<MoveHandle> MoveTable::handleOf(std::string_view id) const {
  const MoveEntry* entry = find(id);
  if (entry == nullptr) {
    return std::nullopt;
  }
  return handleOf(*entry);
}

MoveHandle MoveTable::handleOf(const MoveEntry& entry) const {
  DWE_ASSERT(&entry >= moves_.data() && &entry < moves_.data() + moves_.size());
  return MoveHandle{generation_, static_cast<std::uint32_t>(&entry - moves_.data())};
}

bool MoveTable::owns(MoveHandle handle) const {
  return handle.generation_ == generation_ && handle.position_ < moves_.size();
}

const MoveEntry& MoveTable::at(MoveHandle handle) const {
  DWE_ASSERT(owns(handle));
  return moves_[handle.position_];
}

const MoveEntry* MoveTable::findByOriginalIndex(std::int32_t index) const {
  const auto found = std::ranges::find_if(moves_, [index](const MoveEntry& entry) {
    return entry.originalIndex && entry.index == index;
  });
  return found == moves_.end() ? nullptr : &*found;
}

core::Result<MoveTable> parseMoveTable(std::string_view yaml,
                                       const std::set<std::string, std::less<>>& engineNumbered) {
  auto tree = parseTableDocument(yaml, messages::MoveTableText);
  if (!tree) {
    return std::unexpected{tree.error()};
  }
  return readMoveTable(*tree, engineNumbered);
}

core::Result<MoveTable> loadMoveTable(const VirtualFileSystem& vfs, const AssetId& id) {
  auto composed = readComposedTableText(vfs, id, MoveComposition);
  if (!composed) {
    return std::unexpected{composed.error()};
  }
  return parseMoveTable(composed->text, composed->engineNumbered);
}

} // namespace dwe::assets
