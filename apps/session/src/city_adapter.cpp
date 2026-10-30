#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/session/city_adapter.hpp>
#include <dwe/session/story_adapter.hpp>

#include <string>
#include <utility>

#include "progress_messages.hpp"

namespace dwe::session {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view ProsperityKey = "prosperity";
constexpr std::string_view ClearedMapKey = "cleared_map";
constexpr std::string_view PointsVariableKey = "points_variable";
constexpr std::string_view RecruitTriggerKey = "recruit_trigger";
constexpr std::string_view FirstSpeciesKey = "first_species";
constexpr std::string_view EndSpeciesKey = "end_species";
constexpr std::string_view MinLevelKey = "min_level";
constexpr std::string_view LevelOffsetKey = "level_offset";
constexpr std::string_view OnePointKey = "one_point_species";
constexpr std::string_view CupVariableKey = "cup_variable";
constexpr std::string_view NoCupFromKey = "no_cup_from";
constexpr std::string_view RegistrationKey = "registration_triggers";
constexpr std::string_view TriggersKey = "triggers";
constexpr std::string_view MapKey = "map";

std::vector<std::int32_t> numbers(YamlReader& reader, const YamlNode& parent,
                                  std::string_view key) {
  std::vector<std::int32_t> values;
  for (const YamlNode& value : reader.sequence(reader.field(parent, key), key)) {
    values.push_back(reader.integer<std::int32_t>(value));
  }
  return values;
}

game::story::ProsperityRules readProsperity(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, progress_messages::ProsperityText,
                         {PointsVariableKey, RecruitTriggerKey, FirstSpeciesKey, EndSpeciesKey,
                          MinLevelKey, LevelOffsetKey, OnePointKey, CupVariableKey, NoCupFromKey,
                          RegistrationKey})) {
    return {};
  }
  const auto integer = [&](std::string_view key) {
    return reader.integer<std::int32_t>(reader.field(node, key));
  };
  return {.pointsVariable = integer(PointsVariableKey),
          .recruitTrigger = integer(RecruitTriggerKey),
          .firstSpecies = integer(FirstSpeciesKey),
          .endSpecies = integer(EndSpeciesKey),
          .minLevel = integer(MinLevelKey),
          .levelOffset = integer(LevelOffsetKey),
          .onePointSpecies = numbers(reader, node, OnePointKey),
          .cupVariable = integer(CupVariableKey),
          .noCupFrom = integer(NoCupFromKey),
          .registrationTriggers = numbers(reader, node, RegistrationKey)};
}

} // namespace

core::Result<CityRules> readCityRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, progress_messages::CitySectionText,
                         {ProsperityKey, ClearedMapKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  CityRules rules;
  rules.prosperity = readProsperity(reader, reader.field(root, ProsperityKey));
  for (const YamlNode& choice : reader.sequence(reader.field(root, ClearedMapKey), ClearedMapKey)) {
    if (!reader.expectKeys(choice, progress_messages::ClearedMapText, {TriggersKey, MapKey})) {
      break;
    }
    rules.clearedMap.push_back({.triggers = numbers(reader, choice, TriggersKey),
                                .map = reader.integer<std::int32_t>(reader.field(choice, MapKey))});
  }
  return reader.finish(std::move(rules), section.file);
}

CityAdapter::CityAdapter(StoryAdapter& story) : story_{&story} {}

ruleset::SectionChecks CityAdapter::sectionChecks() const {
  return {{std::string{CitySection}, [](const ruleset::Section& section) -> core::Result<void> {
             auto rules = readCityRules(section);
             if (!rules) {
               return std::unexpected{rules.error()};
             }
             return {};
           }}};
}

core::Result<void> CityAdapter::readRules(const ruleset::Ruleset& rules) {
  rules_.reset();
  levels_.clear();
  const ruleset::Section* section = rules.section(CitySection);
  if (section == nullptr) {
    core::logDebug("[port:city] the ruleset has no city section: the prosperity is the scripts'");
    return {};
  }
  auto read = readCityRules(*section);
  if (!read) {
    return std::unexpected{read.error()};
  }
  rules_ = std::move(*read);
  if (const assets::DigimonTable* table = story_->digimonTable()) {
    for (const assets::DigimonRow& row : table->digimon) {
      levels_.emplace(row.index, row.level);
    }
  }
  return {};
}

core::Result<void> CityAdapter::startNewGame(GameSession& /*session*/) {
  recalculate();
  return {};
}

void CityAdapter::beforeSave(GameSession& /*session*/) {
  recalculate();
}

void CityAdapter::afterLoad(GameSession& /*session*/) {
  recalculate();
}

void CityAdapter::recalculate() {
  if (!rules_) {
    return;
  }
  auto points =
      game::story::recalculateProsperity(story_->state(), rules_->prosperity, [this](auto species) {
        const auto found = levels_.find(species);
        return found == levels_.end() ? 0 : found->second;
      });
  if (!points) {
    core::logWarning(
        "{}", core::formatMessage(progress_messages::ProsperityFailed, points.error().message));
  }
}

std::optional<std::int32_t> CityAdapter::clearedMap() const {
  if (!rules_) {
    return std::nullopt;
  }
  auto map = game::story::clearedCityMap(story_->state(), rules_->clearedMap);
  if (!map) {
    core::logWarning("{}",
                     core::formatMessage(progress_messages::ClearedMapFailed, map.error().message));
    return std::nullopt;
  }
  core::logDebug("[port:city] getFileCityTopMap (map.c:5592): screen {}", *map);
  return *map;
}

} // namespace dwe::session
