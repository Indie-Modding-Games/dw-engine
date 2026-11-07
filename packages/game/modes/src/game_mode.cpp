#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/modes/game_mode.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

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

} // namespace

core::Result<GameMode> parseGameMode(std::string_view yaml, std::string_view folder) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{tree.error()};
  }
  const YamlNode& root = *tree;
  YamlReader reader;
  if (!reader.expectKeys(root, messages::ModeText,
                         {IdKey, NameKey, DescriptionKey, RulesetKey, RulesetsKey, OrderKey})) {
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
  return reader.finish(std::move(mode), messages::ModeText);
}

} // namespace dwe::game::modes
