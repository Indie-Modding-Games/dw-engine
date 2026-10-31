#include <dwe/assets/yaml_reader.hpp>
#include <dwe/game/tournament/tournament.hpp>

#include <algorithm>
#include <format>
#include <string_view>

namespace dwe::game::tournament {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view SectionText = "the tournament section";
constexpr std::string_view StadiumText = "data/stadium";
constexpr std::string_view StoryKey = "story";
constexpr std::string_view CupVariableKey = "cup_variable";
constexpr std::string_view ResultVariableKey = "result_variable";
constexpr std::string_view RegisteredTriggerKey = "registered_trigger";
constexpr std::string_view WonTriggerBaseKey = "won_trigger_base";
constexpr std::string_view RecruitTriggerBaseKey = "recruit_trigger_base";
constexpr std::string_view RivalsKey = "rivals";
constexpr std::string_view SpeciesCountKey = "species_count";
constexpr std::string_view WholeListCupKey = "whole_list_cup";
constexpr std::string_view WinsNeededKey = "wins_needed";
constexpr std::string_view CupWinsNeededKey = "cup_wins_needed";
constexpr std::string_view StandInsKey = "stand_ins";
constexpr std::string_view TimeLimitKey = "time_limit";
constexpr std::string_view RecoveryDivisorKey = "recovery_divisor";
constexpr std::string_view StatsKey = "stats";
constexpr std::string_view SpreadKey = "spread";
constexpr std::string_view VitalityFactorKey = "vitality_factor";
constexpr std::string_view StatCapKey = "stat_cap";
constexpr std::string_view VitalityCapKey = "vitality_cap";
constexpr std::string_view LevelsKey = "levels";
constexpr std::string_view FirstLevelKey = "first_level";
constexpr std::string_view MovesKey = "moves";
constexpr std::string_view SlotsKey = "slots";
constexpr std::string_view FinishersKey = "finishers";
constexpr std::string_view MpCostFactorKey = "mp_cost_factor";
constexpr std::string_view MpDivisorKey = "mp_divisor";
constexpr std::string_view PrioritiesKey = "priorities";
constexpr std::string_view OpponentBaseStatsKey = "opponent_base_stats";
constexpr std::string_view WinChancesKey = "win_chances";
// The registration's part, which parseScheduleRules reads.
constexpr std::string_view ScheduleKey = "schedule";
constexpr std::size_t PairSize = 2;
constexpr std::size_t LevelSize = 3;

std::int32_t integer(YamlReader& reader, const YamlNode& parent, std::string_view key) {
  return reader.integer<std::int32_t>(reader.field(parent, key));
}

std::vector<std::int32_t> integers(YamlReader& reader, const YamlNode& node,
                                   std::string_view what) {
  std::vector<std::int32_t> values;
  for (const YamlNode& value : reader.sequence(node, what)) {
    values.push_back(reader.integer<std::int32_t>(value));
  }
  return values;
}

std::vector<std::pair<std::int32_t, std::int32_t>> pairs(YamlReader& reader, const YamlNode& parent,
                                                         std::string_view key) {
  std::vector<std::pair<std::int32_t, std::int32_t>> values;
  for (const YamlNode& pair : reader.sequence(reader.field(parent, key), key)) {
    const auto both = reader.sequence(pair, key, PairSize);
    if (both.size() == PairSize) {
      values.emplace_back(reader.integer<std::int32_t>(both[0]),
                          reader.integer<std::int32_t>(both[1]));
    }
  }
  return values;
}

void readStory(YamlReader& reader, const YamlNode& root, TournamentRules& rules) {
  const YamlNode& story = reader.field(root, StoryKey);
  if (reader.expectKeys(story, StoryKey,
                        {CupVariableKey, ResultVariableKey, RegisteredTriggerKey, WonTriggerBaseKey,
                         RecruitTriggerBaseKey})) {
    rules.cupVariable = integer(reader, story, CupVariableKey);
    rules.resultVariable = integer(reader, story, ResultVariableKey);
    rules.registeredTrigger = integer(reader, story, RegisteredTriggerKey);
    rules.wonTriggerBase = integer(reader, story, WonTriggerBaseKey);
    rules.recruitTriggerBase = integer(reader, story, RecruitTriggerBaseKey);
  }
  const YamlNode& rivals = reader.field(root, RivalsKey);
  if (reader.expectKeys(rivals, RivalsKey, {SpeciesCountKey, WholeListCupKey})) {
    rules.speciesCount = integer(reader, rivals, SpeciesCountKey);
    rules.wholeListCup = integer(reader, rivals, WholeListCupKey);
  }
}

void readRivals(YamlReader& reader, const YamlNode& root, TournamentRules& rules) {
  const YamlNode& stats = reader.field(root, StatsKey);
  if (reader.expectKeys(stats, StatsKey,
                        {SpreadKey, VitalityFactorKey, StatCapKey, VitalityCapKey})) {
    rules.statSpread = integer(reader, stats, SpreadKey);
    rules.vitalityFactor = integer(reader, stats, VitalityFactorKey);
    rules.statCap = integer(reader, stats, StatCapKey);
    rules.vitalityCap = integer(reader, stats, VitalityCapKey);
  }
  for (const YamlNode& level : reader.sequence(reader.field(root, LevelsKey), LevelsKey)) {
    const auto values = reader.sequence(level, LevelsKey, LevelSize);
    if (values.size() == LevelSize) {
      rules.levels.push_back({.level = reader.integer<std::int32_t>(values[0]),
                              .multiply = reader.integer<std::int32_t>(values[1]),
                              .divide = reader.integer<std::int32_t>(values[2])});
    }
  }
  rules.firstLevel = integer(reader, root, FirstLevelKey);
  const YamlNode& moves = reader.field(root, MovesKey);
  if (reader.expectKeys(moves, MovesKey,
                        {SlotsKey, FinishersKey, MpCostFactorKey, MpDivisorKey, PrioritiesKey})) {
    rules.moveSlots = integer(reader, moves, SlotsKey);
    const auto finishers = reader.sequence(reader.field(moves, FinishersKey), FinishersKey, 2);
    if (finishers.size() == 2) {
      rules.finishers = {reader.integer<std::int32_t>(finishers[0]),
                         reader.integer<std::int32_t>(finishers[1])};
    }
    rules.mpCostFactor = integer(reader, moves, MpCostFactorKey);
    rules.mpDivisor = integer(reader, moves, MpDivisorKey);
    const auto priorities = reader.sequence(reader.field(moves, PrioritiesKey), PrioritiesKey,
                                            rules.movePriorities.size());
    for (std::size_t slot = 0; slot < priorities.size() && slot < rules.movePriorities.size();
         ++slot) {
      rules.movePriorities.at(slot) = reader.integer<std::int32_t>(priorities[slot]);
    }
  }
}

} // namespace

core::Result<TournamentRules> parseTournamentRules(const ruleset::Section& section,
                                                   const core::YamlNode& stadium) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, SectionText,
                         {StoryKey, RivalsKey, WinsNeededKey, CupWinsNeededKey, StandInsKey,
                          TimeLimitKey, RecoveryDivisorKey, StatsKey, LevelsKey, FirstLevelKey,
                          MovesKey, ScheduleKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  TournamentRules rules;
  readStory(reader, root, rules);
  rules.winsNeeded = integer(reader, root, WinsNeededKey);
  rules.cupWinsNeeded = pairs(reader, root, CupWinsNeededKey);
  rules.standIns = pairs(reader, root, StandInsKey);
  rules.timeLimit = integer(reader, root, TimeLimitKey);
  rules.recoveryDivisor = integer(reader, root, RecoveryDivisorKey);
  readRivals(reader, root, rules);
  if (!reader.ok()) {
    return std::unexpected{reader.error(section.file)};
  }
  if (rules.recoveryDivisor <= 0 || rules.mpDivisor <= 0 ||
      std::ranges::any_of(rules.levels,
                          [](const LevelScale& scale) { return scale.divide <= 0; })) {
    return core::fail(std::format("{}: a divisor is not positive", section.file));
  }
  // data/stadium: the tables of the executable's STD overlay; it has more than these.
  YamlReader tables;
  rules.opponentBaseStats =
      integers(tables, tables.field(stadium, OpponentBaseStatsKey), OpponentBaseStatsKey);
  for (const YamlNode& row : tables.sequence(tables.field(stadium, WinChancesKey), WinChancesKey)) {
    rules.winChances.push_back(integers(tables, row, WinChancesKey));
  }
  if (!tables.ok()) {
    return std::unexpected{tables.error(StadiumText)};
  }
  return rules;
}

} // namespace dwe::game::tournament
