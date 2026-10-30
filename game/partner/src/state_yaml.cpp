#include <dwe/game/partner/messages.hpp>
#include <dwe/game/partner/state_yaml.hpp>
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

constexpr std::string_view NullText = "null";
constexpr std::string_view TrueText = "true";
constexpr std::string_view FalseText = "false";

constexpr std::string_view VersionKey = "version";
constexpr std::string_view SpeciesKey = "species";
constexpr std::string_view StatsKey = "stats";
constexpr std::string_view CurrentHpKey = "current_hp";
constexpr std::string_view CurrentMpKey = "current_mp";
constexpr std::string_view ChargeModeKey = "charge_mode";
constexpr std::string_view ConditionsKey = "conditions";
constexpr std::string_view SleepKey = "sleep";
constexpr std::string_view HungerKey = "hunger";
constexpr std::string_view ToiletKey = "toilet";
constexpr std::string_view HealthKey = "health";
constexpr std::string_view MoodKey = "mood";
constexpr std::string_view LifeKey = "life";
constexpr std::string_view EvolutionKey = "evolution";
constexpr std::string_view TrainingKey = "training";
constexpr std::string_view WeightKey = "weight";
constexpr std::string_view VirusBackupKey = "virus_backup";
constexpr std::string_view VirusBackupStatsKey = "stats";

constexpr std::string_view SleepyHourKey = "sleepy_hour";
constexpr std::string_view SleepyMinuteKey = "sleepy_minute";
constexpr std::string_view WakeHourKey = "wake_hour";
constexpr std::string_view WakeMinuteKey = "wake_minute";
constexpr std::string_view AwakeHoursKey = "awake_hours";
constexpr std::string_view AsleepHoursKey = "asleep_hours";
constexpr std::string_view AwakeCutsKey = "awake_cuts";
constexpr std::string_view TirednessCounterKey = "tiredness_counter";
constexpr std::string_view SleepyHoursForSicknessKey = "sleepy_hours_for_sickness";
constexpr std::string_view MissedSleepHoursKey = "missed_sleep_hours";

constexpr std::string_view FoodLevelKey = "food_level";
constexpr std::string_view NextHungerHourKey = "next_hunger_hour";
constexpr std::string_view WindowStepsKey = "window_steps";
constexpr std::string_view EnergyKey = "energy";
constexpr std::string_view EmptyStomachKey = "empty_stomach_minutes";
constexpr std::string_view RefusedFavoriteKey = "refused_favorite";
constexpr std::string_view TiredHungerFramesKey = "tired_hunger_frames";

constexpr std::string_view PoopLevelKey = "poop_level";
constexpr std::string_view UrgeFramesKey = "urge_frames";
constexpr std::string_view VirusBarKey = "virus_bar";
// Optional: saves from before it have no pile waiting for its scene.
constexpr std::string_view WildPoopDueKey = "wild_poop_due";

constexpr std::string_view SicknessTriesKey = "sickness_tries";
constexpr std::string_view SickHoursKey = "sick_hours";
constexpr std::string_view InjuredHoursKey = "injured_hours";
constexpr std::string_view TimesSickKey = "times_sick";

constexpr std::string_view SubTirednessKey = "sub_tiredness";
constexpr std::string_view AreaFramesKey = "area_frames";
constexpr std::string_view ScoldBoostKey = "scold_boost";

constexpr std::string_view RemainingLifetimeKey = "remaining_lifetime";
constexpr std::string_view AgeKey = "age";
constexpr std::string_view LivesKey = "lives";
constexpr std::string_view ProtectedHourKey = "protected_hour";
constexpr std::string_view PhaseKey = "phase";

constexpr std::string_view CareMistakesKey = "care_mistakes";
constexpr std::string_view BattlesKey = "battles";
constexpr std::string_view HoursKey = "hours";
constexpr std::string_view FishCaughtKey = "fish_caught";

constexpr std::string_view BoostKey = "boost";
constexpr std::string_view BoostStatsKey = "stats";
constexpr std::string_view BoostValueKey = "value";
constexpr std::string_view BoostFramesKey = "frames";
constexpr std::string_view YoungSessionsKey = "young_sessions";

constexpr std::string_view ConditionText = "condition";
constexpr std::string_view ScoldBoostText = "scold boost";
constexpr std::string_view PhaseText = "life phase";

constexpr std::array<std::string_view, ConditionCount> ConditionNames{
    "sleepy", "tired", "hungry", "needs_toilet", "sulking", "injured", "sick"};
constexpr std::array<std::string_view, 3> ScoldBoostNames{"none", "ready", "used"};
constexpr std::array<std::string_view, 3> PhaseNames{"alive", "died_naturally", "died_otherwise"};

YamlNode number(std::int64_t value) {
  return YamlNode::scalar(std::to_string(value));
}

YamlNode text(std::string_view value) {
  return YamlNode::scalar(std::string{value}, YamlNode::ScalarStyle::Quoted);
}

void put(YamlNode& mapping, std::string_view key, std::int64_t value) {
  mapping.set(std::string{key}, number(value));
}

YamlNode statsNode(const StatBlock& stats) {
  YamlNode node = YamlNode::mapping();
  for (const Stat stat : AllStats) {
    put(node, statName(stat), stats[stat]);
  }
  return node;
}

template <typename Enum, std::size_t Count>
YamlNode enumNode(const std::array<std::string_view, Count>& names, Enum value) {
  return YamlNode::scalar(std::string{names.at(static_cast<std::size_t>(value))});
}

YamlNode virusBackupNode(const VirusBackup& backup) {
  YamlNode node = YamlNode::mapping();
  node.set(std::string{SpeciesKey}, text(backup.species));
  node.set(std::string{VirusBackupStatsKey}, statsNode(backup.stats));
  return node;
}

YamlNode sleepNode(const SleepState& sleep) {
  YamlNode node = YamlNode::mapping();
  put(node, SleepyHourKey, sleep.sleepyHour);
  put(node, SleepyMinuteKey, sleep.sleepyMinute);
  put(node, WakeHourKey, sleep.wakeHour);
  put(node, WakeMinuteKey, sleep.wakeMinute);
  put(node, AwakeHoursKey, sleep.awakeHours);
  put(node, AsleepHoursKey, sleep.asleepHours);
  put(node, AwakeCutsKey, sleep.awakeCuts);
  put(node, TirednessCounterKey, sleep.tirednessCounter);
  put(node, SleepyHoursForSicknessKey, sleep.sleepyHoursForSickness);
  put(node, MissedSleepHoursKey, sleep.missedSleepHours);
  return node;
}

YamlNode hungerNode(const HungerState& hunger) {
  YamlNode node = YamlNode::mapping();
  put(node, FoodLevelKey, hunger.foodLevel);
  put(node, NextHungerHourKey, hunger.nextHungerHour);
  put(node, WindowStepsKey, hunger.windowSteps);
  put(node, EnergyKey, hunger.energy);
  put(node, EmptyStomachKey, hunger.emptyStomachMinutes);
  node.set(std::string{RefusedFavoriteKey},
           YamlNode::scalar(std::string{hunger.refusedFavorite ? TrueText : FalseText}));
  put(node, TiredHungerFramesKey, hunger.tiredHungerFrames);
  return node;
}

YamlNode toiletNode(const ToiletState& toilet) {
  YamlNode node = YamlNode::mapping();
  put(node, PoopLevelKey, toilet.poopLevel);
  put(node, UrgeFramesKey, toilet.urgeFrames);
  put(node, VirusBarKey, toilet.virusBar);
  node.set(std::string{WildPoopDueKey},
           YamlNode::scalar(std::string{toilet.wildPoopDue ? TrueText : FalseText}));
  return node;
}

YamlNode healthNode(const HealthState& health) {
  YamlNode node = YamlNode::mapping();
  put(node, SicknessTriesKey, health.sicknessTries);
  put(node, SickHoursKey, health.sickHours);
  put(node, InjuredHoursKey, health.injuredHours);
  put(node, TimesSickKey, health.timesSick);
  return node;
}

YamlNode moodNode(const MoodState& mood) {
  YamlNode node = YamlNode::mapping();
  put(node, HappinessKey, mood.happiness);
  put(node, DisciplineKey, mood.discipline);
  put(node, TirednessKey, mood.tiredness);
  put(node, SubTirednessKey, mood.subTiredness);
  put(node, AreaFramesKey, mood.areaFrames);
  node.set(std::string{ScoldBoostKey}, enumNode(ScoldBoostNames, mood.scoldBoost));
  return node;
}

YamlNode lifeNode(const LifeState& life) {
  YamlNode node = YamlNode::mapping();
  put(node, RemainingLifetimeKey, life.remainingLifetime);
  put(node, AgeKey, life.age);
  put(node, LivesKey, life.lives);
  node.set(std::string{ProtectedHourKey}, life.protectedHour
                                              ? number(*life.protectedHour)
                                              : YamlNode::scalar(std::string{NullText}));
  node.set(std::string{PhaseKey}, enumNode(PhaseNames, life.phase));
  return node;
}

YamlNode evolutionNode(const EvolutionCounters& evolution) {
  YamlNode node = YamlNode::mapping();
  put(node, CareMistakesKey, evolution.careMistakes);
  put(node, BattlesKey, evolution.battles);
  put(node, HoursKey, evolution.hours);
  put(node, FishCaughtKey, evolution.fishCaught);
  return node;
}

YamlNode trainingNode(const TrainingState& training) {
  YamlNode boost = YamlNode::mapping();
  YamlNode stats = YamlNode::sequence();
  for (const Stat stat : AllStats) {
    if (training.boost.stats.contains(stat)) {
      stats.append(YamlNode::scalar(std::string{statName(stat)}));
    }
  }
  boost.set(std::string{BoostStatsKey}, std::move(stats));
  put(boost, BoostValueKey, training.boost.value);
  put(boost, BoostFramesKey, training.boost.frames);
  YamlNode node = YamlNode::mapping();
  node.set(std::string{BoostKey}, std::move(boost));
  node.set(std::string{YoungSessionsKey}, statsNode(training.youngSessions));
  return node;
}

// Reading.

template <typename Enum, std::size_t Count>
Enum readEnum(YamlReader& reader, const YamlNode& parent, std::string_view key,
              const std::array<std::string_view, Count>& names, std::string_view what) {
  const YamlNode& node = reader.field(parent, key);
  if (!reader.ok()) {
    return Enum{};
  }
  const std::string name = reader.text(node);
  for (std::size_t index = 0; index < Count; ++index) {
    if (names.at(index) == name) {
      return static_cast<Enum>(index);
    }
  }
  reader.fail(node, formatMessage(messages::UnknownEnum, what, name));
  return Enum{};
}

bool expectStats(YamlReader& reader, const YamlNode& node, std::string_view what) {
  return reader.expectKeys(node, what,
                           {statName(Stat::Hp), statName(Stat::Mp), statName(Stat::Offense),
                            statName(Stat::Defense), statName(Stat::Speed), statName(Stat::Brain)});
}

StatBlock readStats(YamlReader& reader, const YamlNode& parent, std::string_view key) {
  StatBlock stats;
  const YamlNode& node = reader.field(parent, key);
  if (!reader.ok() || !expectStats(reader, node, key)) {
    return stats;
  }
  for (const Stat stat : AllStats) {
    stats[stat] = integer(reader, node, statName(stat));
  }
  return stats;
}

VirusBackup readVirusBackup(YamlReader& reader, const YamlNode& root) {
  const YamlNode* node = nullptr;
  if (!group(reader, root, VirusBackupKey, {SpeciesKey, VirusBackupStatsKey}, node)) {
    return {};
  }
  VirusBackup backup;
  const YamlNode& species = reader.field(*node, SpeciesKey);
  backup.species = reader.ok() ? reader.text(species) : std::string{};
  backup.stats = readStats(reader, *node, VirusBackupStatsKey);
  return backup;
}

PartnerStats readPartnerStats(YamlReader& reader, const YamlNode& root) {
  PartnerStats stats;
  const YamlNode& node = reader.field(root, StatsKey);
  if (!reader.ok() ||
      !reader.expectKeys(node, StatsKey,
                         {statName(Stat::Hp), statName(Stat::Mp), statName(Stat::Offense),
                          statName(Stat::Defense), statName(Stat::Speed), statName(Stat::Brain),
                          CurrentHpKey, CurrentMpKey},
                         {ChargeModeKey})) {
    return stats;
  }
  for (const Stat stat : AllStats) {
    stats.base[stat] = integer(reader, node, statName(stat));
  }
  stats.currentHp = integer(reader, node, CurrentHpKey);
  stats.currentMp = integer(reader, node, CurrentMpKey);
  if (node.find(ChargeModeKey) != nullptr) {
    stats.chargeMode = integer(reader, node, ChargeModeKey);
  }
  return stats;
}

Conditions readConditions(YamlReader& reader, const YamlNode& root) {
  Conditions conditions;
  const YamlNode& node = reader.field(root, ConditionsKey);
  if (!reader.ok()) {
    return conditions;
  }
  for (const YamlNode& item : reader.sequence(node, ConditionsKey)) {
    const std::string name = reader.text(item);
    const auto found = std::ranges::find(ConditionNames, name);
    if (found == ConditionNames.end()) {
      reader.fail(item, formatMessage(messages::UnknownEnum, ConditionText, name));
      continue;
    }
    conditions.set(static_cast<Condition>(found - ConditionNames.begin()));
  }
  return conditions;
}

SleepState readSleep(YamlReader& reader, const YamlNode& root) {
  const YamlNode* node = nullptr;
  if (!group(reader, root, SleepKey,
             {SleepyHourKey, SleepyMinuteKey, WakeHourKey, WakeMinuteKey, AwakeHoursKey,
              AsleepHoursKey, AwakeCutsKey, TirednessCounterKey, SleepyHoursForSicknessKey,
              MissedSleepHoursKey},
             node)) {
    return {};
  }
  return {.sleepyHour = integer(reader, *node, SleepyHourKey),
          .sleepyMinute = integer(reader, *node, SleepyMinuteKey),
          .wakeHour = integer(reader, *node, WakeHourKey),
          .wakeMinute = integer(reader, *node, WakeMinuteKey),
          .awakeHours = integer(reader, *node, AwakeHoursKey),
          .asleepHours = integer(reader, *node, AsleepHoursKey),
          .awakeCuts = integer(reader, *node, AwakeCutsKey),
          .tirednessCounter = integer(reader, *node, TirednessCounterKey),
          .sleepyHoursForSickness = integer(reader, *node, SleepyHoursForSicknessKey),
          .missedSleepHours = integer(reader, *node, MissedSleepHoursKey)};
}

HungerState readHunger(YamlReader& reader, const YamlNode& root) {
  const YamlNode* node = nullptr;
  if (!group(reader, root, HungerKey,
             {FoodLevelKey, NextHungerHourKey, WindowStepsKey, EnergyKey, EmptyStomachKey,
              RefusedFavoriteKey, TiredHungerFramesKey},
             node)) {
    return {};
  }
  return {.foodLevel = integer(reader, *node, FoodLevelKey),
          .nextHungerHour = integer(reader, *node, NextHungerHourKey),
          .windowSteps = integer(reader, *node, WindowStepsKey),
          .energy = integer(reader, *node, EnergyKey),
          .emptyStomachMinutes = integer(reader, *node, EmptyStomachKey),
          .refusedFavorite = flag(reader, *node, RefusedFavoriteKey),
          .tiredHungerFrames = integer(reader, *node, TiredHungerFramesKey)};
}

ToiletState readToilet(YamlReader& reader, const YamlNode& root) {
  const YamlNode& node = reader.field(root, ToiletKey);
  if (!reader.ok() ||
      !reader.expectKeys(node, ToiletKey, {PoopLevelKey, UrgeFramesKey, VirusBarKey},
                         {WildPoopDueKey})) {
    return {};
  }
  const YamlNode* due = node.find(WildPoopDueKey);
  return {.poopLevel = integer(reader, node, PoopLevelKey),
          .urgeFrames = integer(reader, node, UrgeFramesKey),
          .virusBar = integer(reader, node, VirusBarKey),
          .wildPoopDue = due != nullptr && reader.boolean(*due)};
}

HealthState readHealth(YamlReader& reader, const YamlNode& root) {
  const YamlNode* node = nullptr;
  if (!group(reader, root, HealthKey,
             {SicknessTriesKey, SickHoursKey, InjuredHoursKey, TimesSickKey}, node)) {
    return {};
  }
  return {.sicknessTries = integer(reader, *node, SicknessTriesKey),
          .sickHours = integer(reader, *node, SickHoursKey),
          .injuredHours = integer(reader, *node, InjuredHoursKey),
          .timesSick = integer(reader, *node, TimesSickKey)};
}

MoodState readMood(YamlReader& reader, const YamlNode& root) {
  const YamlNode* node = nullptr;
  if (!group(reader, root, MoodKey,
             {HappinessKey, DisciplineKey, TirednessKey, SubTirednessKey, AreaFramesKey,
              ScoldBoostKey},
             node)) {
    return {};
  }
  return {.happiness = integer(reader, *node, HappinessKey),
          .discipline = integer(reader, *node, DisciplineKey),
          .tiredness = integer(reader, *node, TirednessKey),
          .subTiredness = integer(reader, *node, SubTirednessKey),
          .areaFrames = integer(reader, *node, AreaFramesKey),
          .scoldBoost =
              readEnum<ScoldBoost>(reader, *node, ScoldBoostKey, ScoldBoostNames, ScoldBoostText)};
}

LifeState readLife(YamlReader& reader, const YamlNode& root) {
  const YamlNode* node = nullptr;
  if (!group(reader, root, LifeKey,
             {RemainingLifetimeKey, AgeKey, LivesKey, ProtectedHourKey, PhaseKey}, node)) {
    return {};
  }
  LifeState life;
  life.remainingLifetime = integer(reader, *node, RemainingLifetimeKey);
  life.age = integer(reader, *node, AgeKey);
  life.lives = integer(reader, *node, LivesKey);
  const YamlNode& hour = reader.field(*node, ProtectedHourKey);
  if (reader.ok()) {
    life.protectedHour = reader.optionalInteger<std::int32_t>(hour);
  }
  life.phase = readEnum<LifePhase>(reader, *node, PhaseKey, PhaseNames, PhaseText);
  return life;
}

EvolutionCounters readEvolution(YamlReader& reader, const YamlNode& root) {
  const YamlNode* node = nullptr;
  if (!group(reader, root, EvolutionKey, {CareMistakesKey, BattlesKey, HoursKey, FishCaughtKey},
             node)) {
    return {};
  }
  return {.careMistakes = integer(reader, *node, CareMistakesKey),
          .battles = integer(reader, *node, BattlesKey),
          .hours = integer(reader, *node, HoursKey),
          .fishCaught = integer(reader, *node, FishCaughtKey)};
}

TrainingState readTraining(YamlReader& reader, const YamlNode& root) {
  const YamlNode* node = nullptr;
  if (!group(reader, root, TrainingKey, {BoostKey, YoungSessionsKey}, node)) {
    return {};
  }
  TrainingState training;
  const YamlNode* boost = nullptr;
  if (group(reader, *node, BoostKey, {BoostStatsKey, BoostValueKey, BoostFramesKey}, boost)) {
    const YamlNode& stats = reader.field(*boost, BoostStatsKey);
    if (reader.ok()) {
      for (const YamlNode& item : reader.sequence(stats, BoostStatsKey)) {
        if (const std::optional<Stat> found = stat(reader, item)) {
          training.boost.stats.add(*found);
        }
      }
    }
    training.boost.value = integer(reader, *boost, BoostValueKey);
    training.boost.frames = integer(reader, *boost, BoostFramesKey);
  }
  training.youngSessions = readStats(reader, *node, YoungSessionsKey);
  return training;
}

} // namespace

YamlNode toYaml(const PartnerState& state) {
  YamlNode root = YamlNode::mapping();
  put(root, VersionKey, PartnerStateVersion);
  root.set(std::string{SpeciesKey}, text(state.species));
  YamlNode stats = statsNode(state.stats.base);
  put(stats, CurrentHpKey, state.stats.currentHp);
  put(stats, CurrentMpKey, state.stats.currentMp);
  // Written only when it is not the 0 every partner has, so saves stay as they were.
  if (state.stats.chargeMode != 0) {
    put(stats, ChargeModeKey, state.stats.chargeMode);
  }
  root.set(std::string{StatsKey}, std::move(stats));
  YamlNode conditions = YamlNode::sequence();
  for (std::size_t index = 0; index < ConditionCount; ++index) {
    if (state.conditions.has(static_cast<Condition>(index))) {
      conditions.append(YamlNode::scalar(std::string{ConditionNames.at(index)}));
    }
  }
  root.set(std::string{ConditionsKey}, std::move(conditions));
  root.set(std::string{SleepKey}, sleepNode(state.sleep));
  root.set(std::string{HungerKey}, hungerNode(state.hunger));
  root.set(std::string{ToiletKey}, toiletNode(state.toilet));
  root.set(std::string{HealthKey}, healthNode(state.health));
  root.set(std::string{MoodKey}, moodNode(state.mood));
  root.set(std::string{LifeKey}, lifeNode(state.life));
  root.set(std::string{EvolutionKey}, evolutionNode(state.evolution));
  root.set(std::string{TrainingKey}, trainingNode(state.training));
  put(root, WeightKey, state.weight);
  root.set(std::string{VirusBackupKey}, virusBackupNode(state.virusBackup));
  return root;
}

core::Result<PartnerState> fromYaml(const YamlNode& node) {
  YamlReader reader;
  if (!reader.expectKeys(node, messages::StateText,
                         {VersionKey, SpeciesKey, StatsKey, ConditionsKey, SleepKey, HungerKey,
                          ToiletKey, HealthKey, MoodKey, LifeKey, EvolutionKey, TrainingKey,
                          WeightKey, VirusBackupKey})) {
    return std::unexpected{reader.error(messages::StateText)};
  }
  const std::int32_t version = integer(reader, node, VersionKey);
  if (reader.ok() && version != PartnerStateVersion) {
    reader.fail(node.find(VersionKey) != nullptr ? *node.find(VersionKey) : node,
                formatMessage(messages::UnsupportedVersion, version, PartnerStateVersion));
  }
  PartnerState state;
  const YamlNode& species = reader.field(node, SpeciesKey);
  state.species = reader.ok() ? reader.text(species) : std::string{};
  state.stats = readPartnerStats(reader, node);
  state.conditions = readConditions(reader, node);
  state.sleep = readSleep(reader, node);
  state.hunger = readHunger(reader, node);
  state.toilet = readToilet(reader, node);
  state.health = readHealth(reader, node);
  state.mood = readMood(reader, node);
  state.life = readLife(reader, node);
  state.evolution = readEvolution(reader, node);
  state.training = readTraining(reader, node);
  state.weight = integer(reader, node, WeightKey);
  state.virusBackup = readVirusBackup(reader, node);
  return reader.finish(std::move(state), messages::StateText);
}

core::Result<YamlNode> addVirusBackupSpecies(YamlNode state) {
  const YamlNode* stats = state.isMapping() ? state.find(VirusBackupKey) : nullptr;
  if (stats == nullptr) {
    return core::fail(formatMessage(messages::MissingStateKey, VirusBackupKey));
  }
  YamlNode backup = YamlNode::mapping();
  backup.set(std::string{SpeciesKey}, text({}));
  backup.set(std::string{VirusBackupStatsKey}, *stats);
  state.set(std::string{VirusBackupKey}, std::move(backup));
  put(state, VersionKey, VirusBackupSpeciesVersion);
  return state;
}

} // namespace dwe::game::partner
