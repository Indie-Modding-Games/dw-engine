#include <dwe/game/partner/messages.hpp>
#include <dwe/game/partner/training_rules.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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

constexpr std::string_view AptitudeTenthsKey = "aptitude_tenths";
constexpr std::string_view LevelTenthsKey = "level_tenths";
constexpr std::string_view UpgradeBonusKey = "upgrade_bonus";
constexpr std::string_view UpgradeFlagsKey = "upgrade_flags";
constexpr std::string_view PracticeBonusKey = "practice_bonus";
constexpr std::string_view PracticeSessionsKey = "practice_sessions";
constexpr std::string_view PracticeLevelBelowKey = "practice_level_below";
constexpr std::string_view ManualBonusKey = "manual_bonus";
constexpr std::string_view TiredOutTirednessKey = "tired_out_tiredness";
constexpr std::string_view TiredOutGainKey = "tired_out_gain";
constexpr std::string_view SlotDefaultKey = "slot_default_tenths";
constexpr std::string_view OverworkTirednessKey = "overwork_tiredness";
constexpr std::string_view OverworkHappinessKey = "overwork_happiness";
constexpr std::string_view OverworkDisciplineKey = "overwork_discipline";
constexpr std::string_view GainFormulaKey = "gain_formula";
constexpr std::string_view PlacesKey = "places";
constexpr std::string_view MultiplierKey = "multiplier";
constexpr std::string_view DivisorKey = "divisor";

constexpr std::string_view IdKey = "id";
constexpr std::string_view HungerWindowKey = "hunger_window_minutes";
constexpr std::string_view FoodMinutesKey = "food_minutes";
constexpr std::string_view PoopStepsKey = "poop_steps";
constexpr std::string_view UrgeFramesKey = "urge_frames";
constexpr std::string_view CountsPracticeKey = "counts_practice";
constexpr std::string_view CoursesKey = "courses";
constexpr std::string_view AptitudeKey = "aptitude";
constexpr std::string_view PracticeBonusesKey = "practice_bonuses";
constexpr std::string_view EnergyKey = "energy";
constexpr std::string_view GainsKey = "gains";
constexpr std::string_view StatKey = "stat";
constexpr std::string_view BaseKey = "base";
constexpr std::string_view BoostedByKey = "boosted_by";

constexpr std::string_view BrainTrainingUsesMp = "brain_training_uses_mp_aptitude";

// The stat names, as expectKeys wants them.
bool expectStatKeys(YamlReader& reader, const YamlNode& node, std::string_view what) {
  return reader.expectKeys(node, what,
                           {statName(Stat::Hp), statName(Stat::Mp), statName(Stat::Offense),
                            statName(Stat::Defense), statName(Stat::Speed), statName(Stat::Brain)});
}

template <std::size_t Count>
std::array<std::int32_t, Count> readTenths(YamlReader& reader, const YamlNode& node,
                                           std::string_view what) {
  std::array<std::int32_t, Count> values{};
  const auto items = reader.sequence(node, what, Count);
  if (!reader.ok()) {
    return values;
  }
  for (std::size_t index = 0; index < Count; ++index) {
    values.at(index) = reader.integer<std::int32_t>(items[index]);
    if (reader.ok() && values.at(index) <= 0) {
      reader.fail(items[index], formatMessage(messages::NotPositive, what));
    }
  }
  return values;
}

Ratio readRatio(YamlReader& reader, const YamlNode& parent, std::string_view key) {
  const YamlNode* node = nullptr;
  if (!group(reader, parent, key, {MultiplierKey, DivisorKey}, node)) {
    return {};
  }
  return {.multiplier = positive(reader, *node, MultiplierKey),
          .divisor = positive(reader, *node, DivisorKey)};
}

std::optional<Stat> readStatField(YamlReader& reader, const YamlNode& parent,
                                  std::string_view key) {
  const YamlNode& node = reader.field(parent, key);
  if (!reader.ok()) {
    return std::nullopt;
  }
  return stat(reader, node);
}

CourseGain readGain(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, messages::GainText, {StatKey, BaseKey, BoostedByKey})) {
    return {};
  }
  return {.stat = readStatField(reader, node, StatKey).value_or(Stat::Hp),
          .base = nonNegative(reader, node, BaseKey),
          .boostedBy = readStatField(reader, node, BoostedByKey).value_or(Stat::Hp)};
}

TrainingCourse readCourse(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, messages::CourseText,
                         {IdKey, AptitudeKey, PracticeBonusesKey, TirednessKey, EnergyKey,
                          HappinessKey, GainsKey})) {
    return {};
  }
  TrainingCourse course;
  const YamlNode& id = reader.field(node, IdKey);
  course.id = reader.ok() ? reader.text(id) : std::string{};
  course.aptitude = readStatField(reader, node, AptitudeKey).value_or(Stat::Hp);
  course.practiceBonuses = flag(reader, node, PracticeBonusesKey);
  course.tiredness = nonNegative(reader, node, TirednessKey);
  course.energy = nonNegative(reader, node, EnergyKey);
  course.happiness = nonNegative(reader, node, HappinessKey);
  const YamlNode& gains = reader.field(node, GainsKey);
  if (reader.ok()) {
    for (const YamlNode& gain : reader.sequence(gains, GainsKey)) {
      course.gains.push_back(readGain(reader, gain));
    }
  }
  return course;
}

TrainingPlace readPlace(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, messages::PlaceText,
                         {IdKey, HungerWindowKey, FoodMinutesKey, PoopStepsKey, UrgeFramesKey,
                          CountsPracticeKey, CoursesKey})) {
    return {};
  }
  TrainingPlace place;
  const YamlNode& id = reader.field(node, IdKey);
  place.id = reader.ok() ? reader.text(id) : std::string{};
  place.hungerWindowMinutes = nonNegative(reader, node, HungerWindowKey);
  place.foodMinutes = nonNegative(reader, node, FoodMinutesKey);
  place.poopSteps = nonNegative(reader, node, PoopStepsKey);
  place.urgeFrames = nonNegative(reader, node, UrgeFramesKey);
  place.countsPractice = flag(reader, node, CountsPracticeKey);
  const YamlNode& courses = reader.field(node, CoursesKey);
  if (reader.ok()) {
    for (const YamlNode& item : reader.sequence(courses, CoursesKey)) {
      TrainingCourse course = readCourse(reader, item);
      if (reader.ok() && place.findCourse(course.id) != nullptr) {
        reader.fail(item, formatMessage(messages::RepeatedId, messages::CourseText, course.id));
      }
      place.courses.push_back(std::move(course));
    }
  }
  return place;
}

void readAptitudes(YamlReader& reader, const YamlNode& root, TrainingRules& rules) {
  const YamlNode& node = reader.field(root, AptitudeTenthsKey);
  if (!reader.ok() || !expectStatKeys(reader, node, AptitudeTenthsKey)) {
    return;
  }
  for (const Stat stat : AllStats) {
    const YamlNode& tiers = reader.field(node, statName(stat));
    if (reader.ok()) {
      rules.aptitudeTenths.at(static_cast<std::size_t>(stat)) =
          readTenths<AptitudeTierCount>(reader, tiers, AptitudeTenthsKey);
    }
  }
}

void readUpgradeFlags(YamlReader& reader, const YamlNode& root, TrainingRules& rules) {
  const YamlNode& node = reader.field(root, UpgradeFlagsKey);
  if (!reader.ok() || !expectStatKeys(reader, node, UpgradeFlagsKey)) {
    return;
  }
  for (const Stat stat : AllStats) {
    rules.upgradeFlags[stat] = nonNegative(reader, node, statName(stat));
  }
}

void readPlaces(YamlReader& reader, const YamlNode& root, TrainingRules& rules) {
  const YamlNode& node = reader.field(root, PlacesKey);
  if (!reader.ok()) {
    return;
  }
  for (const YamlNode& item : reader.sequence(node, PlacesKey)) {
    TrainingPlace place = readPlace(reader, item);
    if (reader.ok() && rules.findPlace(place.id) != nullptr) {
      reader.fail(item, formatMessage(messages::RepeatedId, messages::PlaceText, place.id));
    }
    rules.places.push_back(std::move(place));
  }
}

TrainingQuirks readTrainingQuirks(YamlReader& reader, const YamlNode& root) {
  const YamlNode& node = reader.field(root, ruleset::QuirksKey);
  if (!reader.ok()) {
    return {};
  }
  const ruleset::Quirks quirks = ruleset::readQuirks(reader, node, {BrainTrainingUsesMp});
  return {.brainTrainingUsesMpAptitude = quirks.on(BrainTrainingUsesMp)};
}

} // namespace

std::string_view statName(Stat stat) {
  constexpr std::array<std::string_view, StatCount> Names{"hp",      "mp",    "offense",
                                                          "defense", "speed", "brain"};
  return Names.at(static_cast<std::size_t>(stat));
}

const TrainingCourse* TrainingPlace::findCourse(std::string_view course) const {
  const auto found = std::ranges::find(courses, course, &TrainingCourse::id);
  return found == courses.end() ? nullptr : &*found;
}

const TrainingPlace* TrainingRules::findPlace(std::string_view place) const {
  const auto found = std::ranges::find(places, place, &TrainingPlace::id);
  return found == places.end() ? nullptr : &*found;
}

Stat TrainingRules::multiplierStat(const TrainingCourse& course) const {
  // trn_reward.c: TRN_awardBrainTrainingGains asks for the multiplier of mode 1, the MP one.
  if (course.aptitude == Stat::Brain && quirks.brainTrainingUsesMpAptitude) {
    return Stat::Mp;
  }
  return course.aptitude;
}

core::Result<TrainingRules> readTrainingRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, messages::TrainingSectionText,
                         {AptitudeTenthsKey, LevelTenthsKey, UpgradeBonusKey, UpgradeFlagsKey,
                          PracticeBonusKey, PracticeSessionsKey, PracticeLevelBelowKey,
                          ManualBonusKey, TiredOutTirednessKey, TiredOutGainKey, SlotDefaultKey,
                          OverworkTirednessKey, OverworkHappinessKey, OverworkDisciplineKey,
                          GainFormulaKey, PlacesKey, ruleset::QuirksKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  TrainingRules rules;
  readAptitudes(reader, root, rules);
  const YamlNode& levels = reader.field(root, LevelTenthsKey);
  if (reader.ok()) {
    rules.levelTenths = readTenths<LevelCount>(reader, levels, LevelTenthsKey);
  }
  rules.upgradeBonus = readRatio(reader, root, UpgradeBonusKey);
  readUpgradeFlags(reader, root, rules);
  rules.practiceBonus = readRatio(reader, root, PracticeBonusKey);
  rules.practiceSessions = nonNegative(reader, root, PracticeSessionsKey);
  rules.practiceLevelBelow = nonNegative(reader, root, PracticeLevelBelowKey);
  rules.manualBonus = readRatio(reader, root, ManualBonusKey);
  rules.tiredOutTiredness = nonNegative(reader, root, TiredOutTirednessKey);
  rules.tiredOutGain = nonNegative(reader, root, TiredOutGainKey);
  rules.slotDefaultTenths = positive(reader, root, SlotDefaultKey);
  rules.overworkTiredness = nonNegative(reader, root, OverworkTirednessKey);
  rules.overworkHappiness = integer(reader, root, OverworkHappinessKey);
  rules.overworkDiscipline = integer(reader, root, OverworkDisciplineKey);
  const YamlNode& formula = reader.field(root, GainFormulaKey);
  rules.gainFormula = reader.ok() ? reader.text(formula) : std::string{};
  readPlaces(reader, root, rules);
  rules.quirks = readTrainingQuirks(reader, root);
  return reader.finish(std::move(rules), section.file);
}

} // namespace dwe::game::partner
