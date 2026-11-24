#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/modes/game_mode.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "messages.hpp"

namespace dwe::game::modes {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view IdKey = "id";
constexpr std::string_view NameKey = "name";
constexpr std::string_view DescriptionKey = "description";
constexpr std::string_view RulesetKey = "ruleset";
constexpr std::string_view RulesetsKey = "rulesets";
constexpr std::string_view OrderKey = "order";
constexpr std::string_view OptionsKey = "options";
constexpr std::string_view RandomizerKey = "randomizer";
constexpr std::string_view BattleKey = "battle";
constexpr std::string_view BattlesKey = "battles";

bool isBattleSystem(std::string_view name) {
  return name == ClassicBattle || name == FolderBattle;
}

std::string battleName(YamlReader& reader, const YamlNode& node) {
  std::string name = reader.text(node);
  if (reader.ok() && !isBattleSystem(name)) {
    reader.fail(node, core::formatMessage(messages::ModeBadBattle, name));
  }
  return name;
}

bool isModeId(std::string_view id) {
  return !id.empty() && id.front() >= 'a' && id.front() <= 'z' &&
         std::ranges::all_of(id, [](char letter) {
           return (letter >= 'a' && letter <= 'z') || (letter >= '0' && letter <= '9') ||
                  letter == '_';
         });
}

// A text that must not be empty.
std::string filledText(YamlReader& reader, const YamlNode& node) {
  std::string text = reader.text(node);
  if (reader.ok() && text.empty()) {
    reader.fail(node, messages::ModeEmptyText);
  }
  return text;
}

std::vector<OptionPin> readPins(YamlReader& reader, const YamlNode& node) {
  std::vector<OptionPin> pins;
  if (!node.isMapping()) {
    reader.fail(node, messages::ModeOptionsNotMapping);
    return pins;
  }
  for (const core::YamlEntry& entry : node.entries()) {
    if (!isModeId(entry.key)) {
      reader.fail(entry.value, core::formatMessage(messages::ModeBadOption, entry.key));
      break;
    }
    if (!entry.value.isScalar() || entry.value.text().empty()) {
      reader.fail(entry.value, core::formatMessage(messages::ModeOptionNotScalar, entry.key));
      break;
    }
    pins.push_back({.option = entry.key, .value = entry.value.text()});
  }
  return pins;
}

} // namespace

std::string_view currentModeId(std::string_view saved) {
  return saved.empty() || saved == LegacyStoryMode ? DefaultMode : saved;
}

bool takesProgressFrom(const GameMode& save, const GameMode& world) {
  if (save.id == world.id) {
    return true;
  }
  return save.options.empty() && std::ranges::all_of(world.rulesets, [&](const std::string& id) {
           return std::ranges::contains(save.rulesets, id);
         });
}

core::Result<GameMode> parseGameMode(std::string_view yaml, std::string_view folder) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{tree.error()};
  }
  const YamlNode& root = *tree;
  YamlReader reader;
  if (!reader.expectKeys(root, messages::ModeText,
                         {IdKey, NameKey, DescriptionKey, RulesetKey, RulesetsKey, OrderKey},
                         {OptionsKey, RandomizerKey, BattleKey, BattlesKey})) {
    return std::unexpected{reader.error(messages::ModeText)};
  }
  GameMode mode;
  const YamlNode& idNode = reader.field(root, IdKey);
  mode.id = reader.text(idNode);
  if (reader.ok() && !isModeId(mode.id)) {
    reader.fail(idNode, core::formatMessage(messages::ModeBadId, mode.id));
  }
  if (reader.ok() && mode.id != folder) {
    reader.fail(idNode, core::formatMessage(messages::ModeIdNotFolder, mode.id, folder));
  }
  mode.nameKey = filledText(reader, reader.field(root, NameKey));
  mode.descriptionKey = filledText(reader, reader.field(root, DescriptionKey));
  const YamlNode& rulesetNode = reader.field(root, RulesetKey);
  mode.ruleset = filledText(reader, rulesetNode);
  for (const YamlNode& item : reader.sequence(reader.field(root, RulesetsKey), RulesetsKey)) {
    std::string ruleset = filledText(reader, item);
    if (reader.ok() && std::ranges::contains(mode.rulesets, ruleset)) {
      reader.fail(item, core::formatMessage(messages::ModeRulesetTwice, ruleset));
    }
    mode.rulesets.push_back(std::move(ruleset));
  }
  if (reader.ok() && !std::ranges::contains(mode.rulesets, mode.ruleset)) {
    reader.fail(rulesetNode, core::formatMessage(messages::ModeRulesetNotListed, mode.ruleset));
  }
  mode.order = reader.integer<std::int32_t>(reader.field(root, OrderKey));
  if (const YamlNode* battle = root.find(BattleKey); battle != nullptr && reader.ok()) {
    mode.battle = battleName(reader, *battle);
    mode.battles = {mode.battle};
  }
  if (const YamlNode* battles = root.find(BattlesKey); battles != nullptr && reader.ok()) {
    mode.battles.clear();
    for (const YamlNode& item : reader.sequence(*battles, BattlesKey)) {
      std::string name = battleName(reader, item);
      if (reader.ok() && std::ranges::contains(mode.battles, name)) {
        reader.fail(item, core::formatMessage(messages::ModeBattleTwice, name));
      }
      mode.battles.push_back(std::move(name));
    }
    if (reader.ok() && !std::ranges::contains(mode.battles, mode.battle)) {
      reader.fail(*battles, core::formatMessage(messages::ModeBattleNotListed, mode.battle));
    }
  }
  if (const YamlNode* options = root.find(OptionsKey); options != nullptr && reader.ok()) {
    mode.options = readPins(reader, *options);
  }
  if (const YamlNode* randomizer = root.find(RandomizerKey); randomizer != nullptr) {
    mode.randomizer = reader.boolean(*randomizer);
  }
  return reader.finish(std::move(mode), messages::ModeText);
}

} // namespace dwe::game::modes
