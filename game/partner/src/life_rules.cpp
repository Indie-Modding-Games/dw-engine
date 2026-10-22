#include <dwe/game/partner/life_rules.hpp>
#include <dwe/game/partner/messages.hpp>
#include <dwe/game/partner/training_rules.hpp>

#include <string>
#include <string_view>
#include <utility>

#include "section_reading.hpp"

namespace dwe::game::partner {

namespace {

using assets::YamlReader;
using core::YamlNode;
using messages::formatMessage;
using namespace reading;

constexpr std::string_view LifetimeHoursKey = "lifetime_hours";
constexpr std::string_view LivesKey = "lives";
constexpr std::string_view UnhappyLifetimeKey = "unhappy_lifetime";
constexpr std::string_view EveryHoursKey = "every_hours";
constexpr std::string_view HappinessBelowKey = "happiness_below";
constexpr std::string_view HappinessDivisorKey = "happiness_divisor";
constexpr std::string_view ExtraHoursKey = "extra_hours";
constexpr std::string_view RebirthKey = "rebirth";
constexpr std::string_view BabyChoiceKey = "baby_choice";
constexpr std::string_view BabiesKey = "babies";
constexpr std::string_view InheritKey = "inherit_percent_per_tamer_level";
constexpr std::string_view DigimonKey = "digimon";

UnhappyLifetime readUnhappyLifetime(YamlReader& reader, const YamlNode& root) {
  const YamlNode* node = nullptr;
  if (!group(reader, root, UnhappyLifetimeKey,
             {EveryHoursKey, HappinessBelowKey, HappinessDivisorKey, ExtraHoursKey}, node)) {
    return {};
  }
  return {.everyHours = positive(reader, *node, EveryHoursKey),
          .happinessBelow = integer(reader, *node, HappinessBelowKey),
          .happinessDivisor = positive(reader, *node, HappinessDivisorKey),
          .extraHours = nonNegative(reader, *node, ExtraHoursKey)};
}

RebirthBaby readBaby(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, messages::RebirthBabyText,
                         {DigimonKey, statName(Stat::Hp), statName(Stat::Mp),
                          statName(Stat::Offense), statName(Stat::Defense), statName(Stat::Speed),
                          statName(Stat::Brain)})) {
    return {};
  }
  RebirthBaby baby;
  const YamlNode& digimon = reader.field(node, DigimonKey);
  baby.digimon = reader.ok() ? reader.text(digimon) : std::string{};
  for (const Stat stat : AllStats) {
    baby.stats[stat] = positive(reader, node, statName(stat));
  }
  return baby;
}

RebirthRules readRebirth(YamlReader& reader, const YamlNode& root) {
  const YamlNode* node = nullptr;
  if (!group(reader, root, RebirthKey, {BabyChoiceKey, BabiesKey, InheritKey}, node)) {
    return {};
  }
  RebirthRules rules;
  const YamlNode& choice = reader.field(*node, BabyChoiceKey);
  rules.babyChoice = reader.ok() ? reader.text(choice) : std::string{};
  const YamlNode& babies = reader.field(*node, BabiesKey);
  if (reader.ok()) {
    for (const YamlNode& baby : reader.sequence(babies, BabiesKey)) {
      rules.babies.push_back(readBaby(reader, baby));
    }
    if (reader.ok() && rules.babies.empty()) {
      reader.fail(babies, formatMessage(messages::NoneListed, BabiesKey));
    }
  }
  rules.inheritPercentPerTamerLevel = nonNegative(reader, *node, InheritKey);
  return rules;
}

} // namespace

core::Result<LifeRules> readLifeRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, messages::LifeSectionText,
                         {LifetimeHoursKey, LivesKey, UnhappyLifetimeKey, RebirthKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  LifeRules rules{.lifetimeHours = positive(reader, root, LifetimeHoursKey),
                  .lives = positive(reader, root, LivesKey),
                  .unhappyLifetime = readUnhappyLifetime(reader, root),
                  .rebirth = readRebirth(reader, root)};
  return reader.finish(std::move(rules), section.file);
}

} // namespace dwe::game::partner
