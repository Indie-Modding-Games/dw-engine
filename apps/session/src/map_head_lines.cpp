#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/session/map_head_lines.hpp>

#include <algorithm>
#include <charconv>
#include <format>
#include <limits>
#include <optional>
#include <set>
#include <string_view>

#include "shop_messages.hpp"

namespace dwe::session {

namespace {

using core::YamlNode;

constexpr std::string_view MapHeadListing = "script/maphead";
// The text keys of MAPHEAD's texts: story_000.text.<offset> (formats::script::luaTextKey).
constexpr std::string_view MapHeadTextSource = "story_000";
constexpr std::string_view JumpOp = "jump_to";
constexpr std::string_view TextOp = "show_textbox";
constexpr std::string_view LabelPrefix = "l_";
constexpr std::string_view HexPrefix = "0x";
constexpr int Hex = 16;

struct ListedInstruction {
  std::string op;
  std::optional<std::uint32_t> target;
};

std::optional<std::uint32_t> hexNumber(std::string_view text, std::string_view prefix) {
  if (!text.starts_with(prefix)) {
    return std::nullopt;
  }
  text.remove_prefix(prefix.size());
  std::uint32_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, Hex);
  if (error != std::errc{} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::uint32_t> field(const YamlNode& node, std::string_view key,
                                   std::string_view prefix) {
  const YamlNode* value = node.find(key);
  return value == nullptr || !value->isScalar() ? std::nullopt : hexNumber(value->text(), prefix);
}

std::string textKey(std::uint32_t offset) {
  return std::format("{}.text.{:04x}", MapHeadTextSource, offset);
}

// The keys of the run of JUMP_TO to texts at `entry`; the run ends where another section or a
// table it points at begins.
std::vector<std::string> lineTable(const std::map<std::uint32_t, ListedInstruction>& instructions,
                                   const std::set<std::uint32_t>& entries, std::uint32_t entry) {
  constexpr std::uint32_t JumpSize = 4;
  std::vector<std::string> keys;
  std::uint32_t firstTarget = std::numeric_limits<std::uint32_t>::max();
  for (auto at = instructions.find(entry);
       at != instructions.end() && at->second.op == JumpOp && at->first < firstTarget &&
       (at->first == entry || !entries.contains(at->first));
       at = instructions.find(at->first + JumpSize)) {
    const std::uint32_t target = at->second.target.value_or(0);
    const auto text = instructions.find(target);
    if (text == instructions.end() || text->second.op != TextOp) {
      break;
    }
    keys.push_back(textKey(target));
    firstTarget = std::min(firstTarget, target);
  }
  return keys;
}

} // namespace

core::Result<MapHeadLineTables> readMapHeadLineTables(const assets::VirtualFileSystem& files) {
  const auto id = assets::AssetId::parse(MapHeadListing);
  if (!id) {
    return std::unexpected{id.error()};
  }
  const auto bytes = files.read(*id);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  const auto tree = core::parseYaml(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
  if (!tree) {
    return std::unexpected{tree.error()};
  }
  const YamlNode* sections = tree->find("sections");
  const YamlNode* listed = tree->find("instructions");
  if (sections == nullptr || listed == nullptr || !sections->isSequence() ||
      !listed->isSequence()) {
    return core::fail(messages::formatMessage(messages::MapHeadListingUnread, MapHeadListing));
  }
  std::map<std::uint32_t, ListedInstruction> instructions;
  for (const YamlNode& instruction : listed->items()) {
    const auto at = field(instruction, "at", HexPrefix);
    const YamlNode* op = instruction.find("op");
    if (at && op != nullptr && op->isScalar()) {
      instructions.emplace(
          *at,
          ListedInstruction{.op = op->text(), .target = field(instruction, "target", LabelPrefix)});
    }
  }
  std::set<std::uint32_t> entries;
  for (const YamlNode& section : sections->items()) {
    if (const auto entry = field(section, "at", LabelPrefix)) {
      entries.insert(*entry);
    }
  }
  MapHeadLineTables tables;
  for (const YamlNode& section : sections->items()) {
    const auto number = field(section, "id", HexPrefix);
    const auto entry = field(section, "at", LabelPrefix);
    if (!number || !entry) {
      continue;
    }
    if (auto keys = lineTable(instructions, entries, *entry); !keys.empty()) {
      tables.emplace(static_cast<std::int32_t>(*number), std::move(keys));
    }
  }
  core::logDebug("[port:shop] resolveMapHeadEntry (script_common.c:1204): {} MAPHEAD line tables",
                 tables.size());
  return tables;
}

} // namespace dwe::session
