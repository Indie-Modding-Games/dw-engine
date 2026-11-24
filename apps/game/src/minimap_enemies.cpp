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

// What the walk has set on its way: the fixed-fight trigger and the predefined enemies' variable
// (StoryFightMarks), as two bits of its state.
constexpr unsigned FixedFightBit = 1U;
constexpr unsigned PredefinedBit = 2U;
constexpr unsigned WalkStates = 4U;

std::optional<std::int64_t> fieldNumber(const core::YamlNode& instruction, std::string_view key) {
  const core::YamlNode* value = instruction.find(key);
  if (value == nullptr || !value->isScalar()) {
    return std::nullopt;
  }
  return numberOf(value->text());
}

// The state after an instruction: set_trigger and unset_trigger of the fixed-fight trigger, and
// set_pstat of the predefined enemies' variable, turn their bits on or off.
unsigned stateAfter(const core::YamlNode& instruction, std::string_view op, unsigned state,
                    const StoryFightMarks& marks) {
  if ((op == "set_trigger" || op == "unset_trigger") &&
      fieldNumber(instruction, "trigger") == marks.fixedFightTrigger) {
    return op == "set_trigger" ? (state | FixedFightBit) : (state & ~FixedFightBit);
  }
  if (op == "set_pstat" && fieldNumber(instruction, "pstat") == marks.predefinedVariable) {
    const std::optional<std::int64_t> value = fieldNumber(instruction, "value");
    return value.value_or(0) != 0 ? (state | PredefinedBit) : (state & ~PredefinedBit);
  }
  return state;
}

// The sections from whose first instruction a start_battle can be reached, following every jump,
// branch, choice, switch and call; with `marks`, only along a path that leaves the battle wild.
std::set<std::int32_t> sectionsReachingBattle(const core::YamlNode& listing,
                                              const std::optional<StoryFightMarks>& marks) {
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
  struct Step {
    std::size_t index;
    unsigned state;
  };
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
    std::vector<bool> seen(code.size() * WalkStates, false);
    std::vector<Step> pending{{.index = start->second, .state = 0}};
    bool fights = false;
    while (!pending.empty() && !fights) {
      const Step step = pending.back();
      pending.pop_back();
      if (step.index >= code.size() || seen[(step.index * WalkStates) + step.state]) {
        continue;
      }
      seen[(step.index * WalkStates) + step.state] = true;
      const core::YamlNode& instruction = code[step.index];
      const std::string_view op = opOf(instruction);
      if (op == BattleOp && (!marks || step.state == 0)) {
        fights = true;
        break;
      }
      const unsigned next = marks ? stateAfter(instruction, op, step.state, *marks) : 0;
      std::vector<std::string> targets;
      targetsOf(instruction, targets);
      for (const std::string& target : targets) {
        if (const auto found = labels.find(target); found != labels.end()) {
          pending.push_back({.index = found->second, .state = next});
        }
      }
      if (std::ranges::find(StopOps, op) == StopOps.end()) {
        pending.push_back({.index = step.index + 1, .state = next});
      }
    }
    if (fights) {
      fighting.insert(*sectionId);
    }
  }
  return fighting;
}

std::optional<core::YamlNode> readListing(const assets::VirtualFileSystem& files,
                                          std::uint16_t script) {
  const std::string id = std::format("script/dg/{:03}", script);
  auto asset = assets::AssetId::parse(id);
  if (!asset) {
    return std::nullopt;
  }
  auto bytes = files.read(*asset);
  if (!bytes) {
    core::logWarning("minimap: the listing {} is not read ({}): no enemy is marked", id,
                     bytes.error().message);
    return std::nullopt;
  }
  auto listing = core::parseYaml(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
  if (!listing) {
    core::logWarning("minimap: the listing {} is not read ({}): no enemy is marked", id,
                     listing.error().message);
    return std::nullopt;
  }
  return std::move(*listing);
}

} // namespace

std::set<std::int32_t> fightingSections(const core::YamlNode& listing) {
  return sectionsReachingBattle(listing, std::nullopt);
}

std::set<std::int32_t> wildFightingSections(const core::YamlNode& listing,
                                            const StoryFightMarks& marks) {
  return sectionsReachingBattle(listing, marks);
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

void MinimapEnemyFinder::read(std::optional<std::uint16_t> script) {
  if (read_ == script) {
    return;
  }
  read_ = script;
  sections_.clear();
  wildSections_.clear();
  if (!script) {
    return;
  }
  const std::optional<core::YamlNode> listing = readListing(*files_, *script);
  if (!listing) {
    return;
  }
  sections_ = fightingSections(*listing);
  wildSections_ = wildFightingSections(*listing, marks_);
}

const std::set<std::int32_t>& MinimapEnemyFinder::sectionsOf(std::optional<std::uint16_t> script) {
  read(script);
  return sections_;
}

const std::set<std::int32_t>&
MinimapEnemyFinder::wildSectionsOf(std::optional<std::uint16_t> script) {
  read(script);
  return wildSections_;
}

} // namespace dwe::app
