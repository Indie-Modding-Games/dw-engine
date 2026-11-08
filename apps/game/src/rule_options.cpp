#include "rule_options.hpp"

#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>

#include <charconv>
#include <iterator>
#include <limits>
#include <set>
#include <system_error>

#include "messages.hpp"

namespace dwe::app {

namespace {

using core::YamlNode;

constexpr std::string_view LevelsKey = "levels";
constexpr std::string_view SectionKey = "section";
constexpr std::string_view PathKey = "path";
constexpr std::string_view ValueKey = "value";
constexpr std::string_view PercentKey = "percent";
constexpr std::int64_t WholePercent = 100;

// The quirks the options turn off (care.yaml, evolution.yaml, partner_moves.yaml, card_shop.yaml).
constexpr std::string_view CareSection = "care";
constexpr std::string_view SicknessWrapPath = "quirks.sickness_chance_wraps";
constexpr std::string_view EvolutionSection = "evolution";
constexpr std::string_view AverageCarryPath = "quirks.selection_average_carries_over";
constexpr std::string_view MovesSection = "partner_moves";
constexpr std::string_view ForgetMaskPath = "quirks.forget_mask_16_bits";
constexpr std::string_view CardShopSection = "card_shop";
constexpr std::string_view StockRepeatsPath = "quirks.stock_repeats";
constexpr std::string_view QuirkOff = "false";

// A value as one text, so two values compare whatever their spelling ("false", false, [1, 2]).
std::string emitted(const YamlNode& node) {
  auto text = core::emitYaml(node);
  return text ? *text : node.text();
}

std::optional<std::string> emittedText(std::string_view yaml) {
  auto node = core::parseYaml(yaml);
  if (!node) {
    return std::nullopt;
  }
  return emitted(*node);
}

std::optional<std::int64_t> wholeNumber(const YamlNode& node) {
  if (!node.isScalar()) {
    return std::nullopt;
  }
  const std::string& text = node.text();
  std::int64_t number = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
  if (error != std::errc{} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return number;
}

core::Result<RuleChange> changeOf(const ruleset::Section& section, const YamlNode& node) {
  const YamlNode* target = node.isMapping() ? node.find(SectionKey) : nullptr;
  const YamlNode* path = node.isMapping() ? node.find(PathKey) : nullptr;
  const YamlNode* value = node.isMapping() ? node.find(ValueKey) : nullptr;
  const YamlNode* percent = node.isMapping() ? node.find(PercentKey) : nullptr;
  if (target == nullptr || !target->isScalar() || path == nullptr || !path->isScalar() ||
      (value == nullptr) == (percent == nullptr)) {
    return std::unexpected{section.error(node, messages::DifficultyChangeShape)};
  }
  RuleChange change{.section = target->text(), .path = path->text(), .value = std::string{}};
  if (value != nullptr) {
    change.value = emitted(*value);
  } else {
    const auto number = wholeNumber(*percent);
    if (!number || *number < 0 || *number > std::numeric_limits<std::int32_t>::max()) {
      return std::unexpected{section.error(*percent, messages::DifficultyPercent)};
    }
    change.value = static_cast<std::int32_t>(*number);
  }
  return change;
}

} // namespace

core::Result<std::vector<RuleChange>> difficultyChanges(const ruleset::Ruleset& rules,
                                                        std::string_view level) {
  std::vector<RuleChange> changes;
  const ruleset::Section* section = rules.section(DifficultySection);
  if (level == DifficultyNormal || section == nullptr) {
    return changes;
  }
  const YamlNode* levels = section->node.isMapping() ? section->node.find(LevelsKey) : nullptr;
  if (levels == nullptr || !levels->isMapping()) {
    return std::unexpected{section->error(section->node, messages::DifficultyNoLevels)};
  }
  const YamlNode* list = levels->find(level);
  if (list == nullptr) {
    return changes;
  }
  if (!list->isSequence()) {
    return std::unexpected{section->error(*list, messages::DifficultyChangeShape)};
  }
  for (const YamlNode& item : list->items()) {
    auto change = changeOf(*section, item);
    if (!change) {
      return std::unexpected{change.error()};
    }
    changes.push_back(std::move(*change));
  }
  return changes;
}

core::Result<std::vector<RuleChange>> ruleChanges(const ruleset::Ruleset& rules,
                                                  const RuleChoices& choices) {
  std::vector<RuleChange> changes;
  if (choices.fairSicknessChance) {
    changes.push_back({.section = std::string{CareSection},
                       .path = std::string{SicknessWrapPath},
                       .value = std::string{QuirkOff}});
  }
  if (choices.fairEvolutionAverage) {
    changes.push_back({.section = std::string{EvolutionSection},
                       .path = std::string{AverageCarryPath},
                       .value = std::string{QuirkOff}});
  }
  if (choices.fairMoveForgetting) {
    changes.push_back({.section = std::string{MovesSection},
                       .path = std::string{ForgetMaskPath},
                       .value = std::string{QuirkOff}});
  }
  if (choices.fairCardStock) {
    changes.push_back({.section = std::string{CardShopSection},
                       .path = std::string{StockRepeatsPath},
                       .value = std::string{QuirkOff}});
  }
  auto difficulty = difficultyChanges(rules, choices.difficulty);
  if (!difficulty) {
    return std::unexpected{difficulty.error()};
  }
  changes.insert(changes.end(), std::make_move_iterator(difficulty->begin()),
                 std::make_move_iterator(difficulty->end()));
  return changes;
}

void RuleOptions::choose(RuleChoices choices) {
  if (choices == choices_) {
    return;
  }
  choices_ = std::move(choices);
  keptRevision_.reset();
}

void RuleOptions::keep(ruleset::RulesetStore& store) {
  if (keptRevision_ == store.revision()) {
    return;
  }
  auto changes = ruleChanges(store.current(), choices_);
  if (!changes) {
    core::logWarning("{}",
                     core::formatMessage(messages::RuleOptionsUnread, changes.error().message));
    changes = std::vector<RuleChange>{};
  }
  std::set<ValueKey> wanted;
  for (const RuleChange& change : *changes) {
    wanted.insert({change.section, change.path});
    apply(store, change);
  }
  for (auto it = changed_.begin(); it != changed_.end();) {
    if (wanted.contains(it->first)) {
      ++it;
      continue;
    }
    restore(store, it->first, it->second);
    it = changed_.erase(it);
  }
  keptRevision_ = store.revision();
}

void RuleOptions::apply(ruleset::RulesetStore& store, const RuleChange& change) {
  const ruleset::Section* section = store.current().section(change.section);
  const YamlNode* node =
      section != nullptr ? ruleset::findValue(section->node, change.path) : nullptr;
  if (node == nullptr) {
    core::logWarning("{}",
                     core::formatMessage(messages::RuleOptionNoValue, change.section, change.path));
    return;
  }
  const std::string current = emitted(*node);
  const ValueKey key{change.section, change.path};
  const auto found = changed_.find(key);
  // The ruleset's own value: the one remembered while the value is still the one set, else what it
  // holds now (the files after a reload, or what the tuning panel set).
  const std::string own =
      found != changed_.end() && found->second.set == current ? found->second.own : current;
  std::string yaml;
  if (const auto* text = std::get_if<std::string>(&change.value)) {
    yaml = *text;
  } else {
    const auto ownNode = core::parseYaml(own);
    const auto number = ownNode ? wholeNumber(*ownNode) : std::nullopt;
    if (!number) {
      core::logWarning(
          "{}", core::formatMessage(messages::RuleOptionNotNumber, change.section, change.path));
      return;
    }
    yaml = std::to_string(*number * std::get<std::int32_t>(change.value) / WholePercent);
  }
  const std::optional<std::string> target = emittedText(yaml);
  if (!target) {
    core::logWarning(
        "{}", core::formatMessage(messages::RuleOptionRefused, change.section, change.path, yaml));
    return;
  }
  if (*target != current) {
    if (auto set = store.setValue(change.section, change.path, yaml); !set) {
      core::logWarning("{}", core::formatMessage(messages::RuleOptionRefused, change.section,
                                                 change.path, set.error().message));
      return;
    }
    core::logInfo("{}",
                  core::formatMessage(messages::RuleOptionSet, change.section, change.path, yaml));
  }
  changed_[key] = {.own = own, .set = *target};
}

void RuleOptions::restore(ruleset::RulesetStore& store, const ValueKey& key,
                          const Changed& changed) {
  const ruleset::Section* section = store.current().section(key.first);
  const YamlNode* node =
      section != nullptr ? ruleset::findValue(section->node, key.second) : nullptr;
  // Someone else changed it since (or a reload put the files' value back): theirs stays.
  if (node == nullptr || emitted(*node) != changed.set) {
    return;
  }
  if (auto set = store.setValue(key.first, key.second, changed.own); !set) {
    core::logWarning("{}", core::formatMessage(messages::RuleOptionRefused, key.first, key.second,
                                               set.error().message));
    return;
  }
  core::logInfo("{}",
                core::formatMessage(messages::RuleOptionSet, key.first, key.second, changed.own));
}

} // namespace dwe::app
