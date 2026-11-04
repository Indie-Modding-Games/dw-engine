#include "minimap_enemies.hpp"

#include <dwe/assets/asset_id.hpp>
#include <dwe/core/log.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace dwe::app {
namespace {

constexpr std::string_view BattleOp = "start_battle";
// The orders after which the script does not go on to the next instruction: a jump, the end of
// the section or of the script, a return from a call, a map change and a jump to another script.
constexpr std::array<std::string_view, 6> StopOps{"jump_to",     "end_section", "end_script",
                                                  "jump_return", "warp_to",     "jump_to_file"};
constexpr std::string_view LabelPrefix = "l_";
// The keys whose value names no place to go: the instruction's own label and its text.
constexpr std::array<std::string_view, 2> NotTargets{"label", "text"};

std::optional<std::int32_t> numberOf(std::string_view text) {
  int base = 10;
  if (text.starts_with("0x")) {
    text.remove_prefix(2);
    base = 16;
  }
  std::int32_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
  if (error != std::errc{} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

bool isLabel(std::string_view text) {
  if (!text.starts_with(LabelPrefix) || text.size() == LabelPrefix.size()) {
    return false;
  }
  text.remove_prefix(LabelPrefix.size());
  return std::ranges::all_of(text,
                             [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); });
}

// Every label a value names: a jump's target, a branch's, a choice's options, a switch's cases.
void targetsOf(const core::YamlNode& node, std::vector<std::string>& targets) {
  if (node.isScalar()) {
    if (isLabel(node.text())) {
      targets.push_back(node.text());
    }
    return;
  }
  if (node.isSequence()) {
    for (const core::YamlNode& item : node.items()) {
      targetsOf(item, targets);
    }
    return;
  }
  for (const core::YamlEntry& entry : node.entries()) {
    if (std::ranges::find(NotTargets, entry.key) == NotTargets.end()) {
      targetsOf(entry.value, targets);
    }
  }
}

std::string_view opOf(const core::YamlNode& instruction) {
  const core::YamlNode* op = instruction.find("op");
  return op != nullptr && op->isScalar() ? std::string_view{op->text()} : std::string_view{};
}

} // namespace

std::set<std::int32_t> fightingSections(const core::YamlNode& listing) {
  std::set<std::int32_t> fighting;
  const core::YamlNode* instructions = listing.find("instructions");
  const core::YamlNode* sections = listing.find("sections");
  if (instructions == nullptr || !instructions->isSequence() || sections == nullptr ||
      !sections->isSequence()) {
    return fighting;
  }
  const std::span<const core::YamlNode> code = instructions->items();
  std::map<std::string, std::size_t, std::less<>> labels;
  for (std::size_t index = 0; index < code.size(); ++index) {
    if (const core::YamlNode* label = code[index].find("label");
        label != nullptr && label->isScalar()) {
      labels.emplace(label->text(), index);
    }
  }
  // Whether a start_battle can be reached from each instruction, worked out once per listing: a
  // walk from each section's start that marks what it reached.
  for (const core::YamlNode& section : sections->items()) {
    const core::YamlNode* id = section.find("id");
    const core::YamlNode* at = section.find("at");
    if (id == nullptr || !id->isScalar() || at == nullptr || !at->isScalar()) {
      continue;
    }
    const std::optional<std::int32_t> sectionId = numberOf(id->text());
    const auto start = labels.find(at->text());
    if (!sectionId || start == labels.end()) {
      continue;
    }
    std::vector<bool> seen(code.size(), false);
    std::vector<std::size_t> pending{start->second};
    bool fights = false;
    while (!pending.empty() && !fights) {
      const std::size_t index = pending.back();
      pending.pop_back();
      if (index >= code.size() || seen[index]) {
        continue;
      }
      seen[index] = true;
      const core::YamlNode& instruction = code[index];
      const std::string_view op = opOf(instruction);
      if (op == BattleOp) {
        fights = true;
        break;
      }
      std::vector<std::string> targets;
      targetsOf(instruction, targets);
      for (const std::string& target : targets) {
        if (const auto found = labels.find(target); found != labels.end()) {
          pending.push_back(found->second);
        }
      }
      if (std::ranges::find(StopOps, op) == StopOps.end()) {
        pending.push_back(index + 1);
      }
    }
    if (fights) {
      fighting.insert(*sectionId);
    }
  }
  return fighting;
}

std::vector<ui::MinimapMarker> minimapEnemies(bool shown, std::span<const assets::MapNpc> records,
                                              const std::set<std::int32_t>& fighting,
                                              const NpcPosition& positionOf) {
  std::vector<ui::MinimapMarker> enemies;
  if (!shown) {
    return enemies;
  }
  std::set<std::int32_t> marked;
  for (const assets::MapNpc& record : records) {
    if (!fighting.contains(record.scriptId) || !marked.insert(record.scriptId).second) {
      continue;
    }
    if (const std::optional<glm::vec3> at = positionOf(record.scriptId)) {
      enemies.push_back(
          ui::MinimapMarker{.x = at->x, .z = at->z, .facingX = 0.0F, .facingZ = -1.0F});
    }
  }
  return enemies;
}

const std::set<std::int32_t>& MinimapEnemyFinder::sectionsOf(std::optional<std::uint16_t> script) {
  if (read_ == script) {
    return sections_;
  }
  read_ = script;
  sections_.clear();
  if (!script) {
    return sections_;
  }
  const std::string id = std::format("script/dg/{:03}", *script);
  auto asset = assets::AssetId::parse(id);
  if (!asset) {
    return sections_;
  }
  auto bytes = files_->read(*asset);
  if (!bytes) {
    core::logWarning("minimap: the listing {} is not read ({}): no enemy is marked", id,
                     bytes.error().message);
    return sections_;
  }
  auto listing = core::parseYaml(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
  if (!listing) {
    core::logWarning("minimap: the listing {} is not read ({}): no enemy is marked", id,
                     listing.error().message);
    return sections_;
  }
  sections_ = fightingSections(*listing);
  return sections_;
}

} // namespace dwe::app
