#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/log.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/story_lock.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::session {

namespace {

using assets::YamlReader;
using core::YamlNode;
using messages::formatMessage;

constexpr std::string_view ActiveStageKey = "active_stage";
constexpr std::string_view ClosedMessageKey = "closed_message";
constexpr std::string_view StagesKey = "stages";
constexpr std::string_view IdKey = "id";
constexpr std::string_view MapsKey = "maps";
constexpr std::string_view ExitsKey = "exits";
constexpr std::string_view SectionsKey = "sections";
constexpr std::string_view MapKey = "map";
constexpr std::string_view OpenKey = "open";

std::string readText(YamlReader& reader, const YamlNode& node) {
  std::string text = reader.ok() ? reader.text(node) : std::string{};
  if (reader.ok() && text.empty()) {
    reader.fail(node, messages::StoryLockEmptyText);
  }
  return text;
}

std::string readPattern(YamlReader& reader, const YamlNode& node) {
  std::string pattern = readText(reader, node);
  const auto wildcard = pattern.find(MapPatternWildcard);
  if (reader.ok() && wildcard != std::string::npos && wildcard + 1 != pattern.size()) {
    reader.fail(node, formatMessage(messages::StoryLockBadPattern, pattern));
  }
  return pattern;
}

std::vector<std::string> readPatterns(YamlReader& reader, const YamlNode& node) {
  std::vector<std::string> patterns;
  for (const YamlNode& item : reader.sequence(node, messages::StoryLockMapsText)) {
    patterns.push_back(readPattern(reader, item));
  }
  return patterns;
}

std::vector<StoryLockExits> readExits(YamlReader& reader, const YamlNode& node) {
  std::vector<StoryLockExits> exits;
  for (const YamlNode& item : reader.sequence(node, messages::StoryLockExitsText)) {
    if (!reader.expectKeys(item, messages::StoryLockExitsText, {MapKey, OpenKey})) {
      break;
    }
    exits.push_back({.map = readText(reader, reader.field(item, MapKey)),
                     .open = readPatterns(reader, reader.field(item, OpenKey))});
  }
  return exits;
}

std::vector<StoryLockSections> readSections(YamlReader& reader, const YamlNode& node) {
  std::vector<StoryLockSections> sections;
  for (const YamlNode& item : reader.sequence(node, messages::StoryLockSectionsText)) {
    if (!reader.expectKeys(item, messages::StoryLockSectionsText, {MapKey, OpenKey})) {
      break;
    }
    StoryLockSections map{.map = readText(reader, reader.field(item, MapKey)), .open = {}};
    for (const YamlNode& cell :
         reader.sequence(reader.field(item, OpenKey), messages::StoryLockSectionsText)) {
      map.open.push_back(reader.integer<std::int32_t>(cell));
    }
    sections.push_back(std::move(map));
  }
  return sections;
}

StoryLockStage readStage(YamlReader& reader, const YamlNode& node) {
  StoryLockStage stage;
  if (!reader.expectKeys(node, messages::StoryLockStageText, {IdKey, MapsKey},
                         {ExitsKey, SectionsKey})) {
    return stage;
  }
  stage.id = readText(reader, reader.field(node, IdKey));
  stage.maps = readPatterns(reader, reader.field(node, MapsKey));
  if (const YamlNode* exits = node.find(ExitsKey)) {
    stage.exits = readExits(reader, *exits);
  }
  if (const YamlNode* sections = node.find(SectionsKey)) {
    stage.sections = readSections(reader, *sections);
  }
  return stage;
}

bool anyMatches(const std::vector<std::string>& patterns, std::string_view map) {
  return std::ranges::any_of(
      patterns, [map](const std::string& pattern) { return mapPatternMatches(pattern, map); });
}

} // namespace

core::Result<StoryLockRules> readStoryLockRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, messages::StoryLockSectionText,
                         {ActiveStageKey, ClosedMessageKey, StagesKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  StoryLockRules rules;
  const YamlNode& active = reader.field(root, ActiveStageKey);
  rules.activeStage = readText(reader, active);
  rules.closedMessage = readText(reader, reader.field(root, ClosedMessageKey));
  for (const YamlNode& item :
       reader.sequence(reader.field(root, StagesKey), messages::StoryLockStageText)) {
    StoryLockStage stage = readStage(reader, item);
    if (reader.ok() && std::ranges::contains(rules.stages, stage.id, &StoryLockStage::id)) {
      reader.fail(item, formatMessage(messages::StoryLockStageTwice, stage.id));
    }
    if (reader.ok() && stage.id == StoryStageAll) {
      reader.fail(item, formatMessage(messages::StoryLockReservedStage, StoryStageAll));
    }
    rules.stages.push_back(std::move(stage));
  }
  if (reader.ok() && !std::ranges::contains(rules.stages, rules.activeStage, &StoryLockStage::id)) {
    reader.fail(active, formatMessage(messages::StoryLockNoStage, rules.activeStage));
  }
  return reader.finish(std::move(rules), section.file);
}

bool mapPatternMatches(std::string_view pattern, std::string_view map) {
  if (pattern.ends_with(MapPatternWildcard)) {
    return map.starts_with(pattern.substr(0, pattern.size() - 1));
  }
  return pattern == map;
}

StoryLock::StoryLock(const StoryLockRules& rules, std::string_view stage)
    : closedMessage_(rules.closedMessage) {
  for (const StoryLockStage& opened : rules.stages) {
    maps_.insert(maps_.end(), opened.maps.begin(), opened.maps.end());
    if (opened.id == stage) {
      stage_ = opened.id;
      exits_ = opened.exits;
      sections_ = opened.sections;
      break;
    }
  }
  core::logDebug("[port:story-lock] stage {}: {} map patterns, {} narrowed exits, {} narrowed "
                 "section maps",
                 stage_, maps_.size(), exits_.size(), sections_.size());
}

bool StoryLock::mapOpen(std::string_view map) const {
  return !locks() || anyMatches(maps_, map);
}

bool StoryLock::exitOpen(std::string_view from, std::string_view to) const {
  if (!locks()) {
    return true;
  }
  if (!mapOpen(to)) {
    return false;
  }
  const auto narrowed = std::ranges::find(exits_, from, &StoryLockExits::map);
  return narrowed == exits_.end() || anyMatches(narrowed->open, to);
}

bool StoryLock::sectionOpen(std::string_view map, std::int32_t section) const {
  if (!locks()) {
    return true;
  }
  const auto narrowed = std::ranges::find(sections_, map, &StoryLockSections::map);
  return narrowed == sections_.end() || std::ranges::contains(narrowed->open, section);
}

core::Result<StoryLock> storyLockOf(const ruleset::Ruleset& rules, std::string_view stage) {
  const ruleset::Section* section = rules.section(StoryLockSection);
  if (section == nullptr || stage == StoryStageAll) {
    core::logInfo("[port:story-lock] stage {}: everything is open",
                  section == nullptr ? "none (no story_lock section)" : StoryStageAll);
    return StoryLock{};
  }
  auto read = readStoryLockRules(*section);
  if (!read) {
    return std::unexpected{read.error()};
  }
  const std::string_view chosen = stage.empty() ? std::string_view{read->activeStage} : stage;
  if (!std::ranges::contains(read->stages, chosen, &StoryLockStage::id)) {
    return core::fail(formatMessage(messages::StoryLockUnknownStage, chosen, section->file));
  }
  StoryLock lock{*read, chosen};
  core::logInfo("[port:story-lock] stage {} ({}): every map of the stages up to it is open", chosen,
                stage.empty() ? "the ruleset's active_stage" : "--story-stage");
  return lock;
}

ruleset::SectionChecks storyLockSectionChecks() {
  return {
      {std::string{StoryLockSection}, [](const ruleset::Section& section) -> core::Result<void> {
         if (auto read = readStoryLockRules(section); !read) {
           return std::unexpected{read.error()};
         }
         return {};
       }}};
}

} // namespace dwe::session
