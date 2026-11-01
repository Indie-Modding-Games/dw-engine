#include <dwe/assets/yaml_reader.hpp>
#include <dwe/game/tournament/tournament.hpp>

#include <algorithm>
#include <format>
#include <map>
#include <string>
#include <string_view>

namespace dwe::game::tournament {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view ScheduleKey = "schedule";
constexpr std::string_view DayVariableKey = "day_variable";
constexpr std::string_view SpeciesVariableKey = "species_variable";
constexpr std::string_view TomorrowFlagKey = "tomorrow_flag";
constexpr std::string_view RegisteredTriggersKey = "registered_triggers";
constexpr std::string_view OverlevelTriggerKey = "overlevel_trigger";
constexpr std::string_view LastMinuteTodayKey = "last_minute_today";
constexpr std::string_view CycleDaysKey = "cycle_days";
constexpr std::string_view RecruitTriggerEndKey = "recruit_trigger_end";
constexpr std::string_view GradeAWonTriggerKey = "grade_a_won_trigger";
constexpr std::string_view LocalListSectionKey = "local_list_section";
constexpr std::string_view LinesKey = "lines";
constexpr std::string_view LinesSectionKey = "section";
constexpr std::string_view TitleKey = "title";
constexpr std::string_view AskKey = "ask";
constexpr std::string_view AskOverlevelKey = "ask_overlevel";
constexpr std::string_view ChoiceKey = "choice";
constexpr std::string_view SoundsKey = "sounds";
constexpr std::string_view SlotKey = "slot";
constexpr std::string_view RefuseKey = "refuse";
constexpr std::string_view TableText = "data/tournaments";
constexpr std::string_view TournamentsKey = "tournaments";
constexpr std::string_view IdKey = "id";
constexpr std::string_view IndexKey = "index";
constexpr std::string_view GradeKey = "grade";
constexpr std::string_view DayKey = "day";
constexpr std::string_view MedalIdsKey = "medal_ids";

// isTournamentEnabled: the requirements list is [triggers needed, wins needed, requirement type].
enum class Requirement : std::uint8_t { None = 0, GradeAWon = 1, LocalList = 2 };
constexpr std::size_t TriggersAt = 0;
constexpr std::size_t WinsAt = 1;
constexpr std::size_t TypeAt = 2;

std::int32_t integer(YamlReader& reader, const YamlNode& parent, std::string_view key) {
  return reader.integer<std::int32_t>(reader.field(parent, key));
}

std::int32_t requirement(const CupData& data, std::size_t at) {
  return at < data.requirements.size() ? data.requirements[at] : 0;
}

std::int32_t countSet(std::span<const std::int32_t> species, std::int32_t base,
                      const std::function<bool(std::int32_t)>& trigger) {
  return static_cast<std::int32_t>(
      std::ranges::count_if(species, [&](std::int32_t number) { return trigger(base + number); }));
}

} // namespace

core::Result<ScheduleRules> parseScheduleRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& schedule = reader.field(section.node, ScheduleKey);
  ScheduleRules rules;
  if (reader.expectKeys(schedule, ScheduleKey,
                        {DayVariableKey, SpeciesVariableKey, TomorrowFlagKey, RegisteredTriggersKey,
                         OverlevelTriggerKey, LastMinuteTodayKey, CycleDaysKey,
                         RecruitTriggerEndKey, GradeAWonTriggerKey, LocalListSectionKey, LinesKey,
                         SoundsKey})) {
    rules.dayVariable = integer(reader, schedule, DayVariableKey);
    rules.speciesVariable = integer(reader, schedule, SpeciesVariableKey);
    rules.tomorrowFlag = integer(reader, schedule, TomorrowFlagKey);
    for (const YamlNode& trigger :
         reader.sequence(reader.field(schedule, RegisteredTriggersKey), RegisteredTriggersKey)) {
      rules.registeredTriggers.push_back(reader.integer<std::int32_t>(trigger));
    }
    rules.overlevelTrigger = integer(reader, schedule, OverlevelTriggerKey);
    rules.lastMinuteToday = integer(reader, schedule, LastMinuteTodayKey);
    rules.cycleDays = integer(reader, schedule, CycleDaysKey);
    rules.recruitTriggerEnd = integer(reader, schedule, RecruitTriggerEndKey);
    rules.gradeAWonTrigger = integer(reader, schedule, GradeAWonTriggerKey);
    rules.localListSection = integer(reader, schedule, LocalListSectionKey);
    const YamlNode& lines = reader.field(schedule, LinesKey);
    if (reader.expectKeys(lines, LinesKey,
                          {LinesSectionKey, TitleKey, AskKey, AskOverlevelKey, ChoiceKey})) {
      rules.linesSection = integer(reader, lines, LinesSectionKey);
      rules.titleLine = integer(reader, lines, TitleKey);
      rules.askLine = integer(reader, lines, AskKey);
      rules.askOverlevelLine = integer(reader, lines, AskOverlevelKey);
      rules.choiceLine = integer(reader, lines, ChoiceKey);
    }
    const YamlNode& sounds = reader.field(schedule, SoundsKey);
    if (reader.expectKeys(sounds, SoundsKey, {SlotKey, RefuseKey})) {
      rules.soundSlot = integer(reader, sounds, SlotKey);
      rules.refuseSound = integer(reader, sounds, RefuseKey);
    }
  }
  if (!reader.ok()) {
    return std::unexpected{reader.error(section.file)};
  }
  if (rules.cycleDays <= 0) {
    return core::fail(std::format("{}: schedule.cycle_days is not positive", section.file));
  }
  return rules;
}

core::Result<TournamentTable> parseTournamentTable(const YamlNode& document) {
  YamlReader reader;
  TournamentTable table;
  std::map<std::string, std::int32_t, std::less<>> numbers;
  for (const YamlNode& cup :
       reader.sequence(reader.field(document, TournamentsKey), TournamentsKey)) {
    const auto index = static_cast<std::size_t>(std::max(integer(reader, cup, IndexKey), 0));
    if (table.grades.size() <= index) {
      table.grades.resize(index + 1);
    }
    table.grades[index] = reader.text(reader.field(cup, GradeKey));
    numbers.emplace(reader.text(reader.field(cup, IdKey)), static_cast<std::int32_t>(index));
  }
  for (const YamlNode& day : reader.sequence(reader.field(document, ScheduleKey), ScheduleKey)) {
    const auto at = static_cast<std::size_t>(std::max(integer(reader, day, DayKey), 0));
    if (table.days.size() <= at) {
      table.days.resize(at + 1);
    }
    for (const YamlNode& id : reader.sequence(reader.field(day, TournamentsKey), TournamentsKey)) {
      const auto found = numbers.find(reader.text(id));
      if (found == numbers.end()) {
        return core::fail(
            std::format("{}: day {} lists an unknown cup '{}'", TableText, at, reader.text(id)));
      }
      table.days[at].push_back(found->second);
    }
  }
  if (const YamlNode* medals = document.isMapping() ? document.find(MedalIdsKey) : nullptr) {
    for (const YamlNode& medal : reader.sequence(*medals, MedalIdsKey)) {
      table.medalIds.push_back(reader.integer<std::int32_t>(medal));
    }
  }
  if (!reader.ok()) {
    return std::unexpected{reader.error(TableText)};
  }
  return table;
}

bool cupOpen(const TournamentRules& rules, const ScheduleRules& schedule, const CupData& data,
             const CupOpening& opening) {
  const std::int32_t triggersNeeded = requirement(data, TriggersAt);
  const std::int32_t winsNeeded = requirement(data, WinsAt);
  const std::int32_t type = requirement(data, TypeAt);
  if (triggersNeeded == 0) {
    return true;
  }
  // The rivals' residents, or every resident trigger when the cup lists no rivals.
  std::int32_t residents = 0;
  if (!data.rivals.empty()) {
    residents = countSet(data.rivals, rules.recruitTriggerBase, opening.trigger);
  } else {
    for (std::int32_t trigger = rules.recruitTriggerBase; trigger < schedule.recruitTriggerEnd;
         ++trigger) {
      residents += opening.trigger(trigger) ? 1 : 0;
    }
  }
  if (residents < triggersNeeded) {
    return false;
  }
  switch (static_cast<Requirement>(type)) {
  case Requirement::None:
    return true;
  case Requirement::GradeAWon:
    return opening.trigger(schedule.gradeAWonTrigger);
  case Requirement::LocalList:
    return countSet(opening.localList, rules.recruitTriggerBase, opening.trigger) >= winsNeeded;
  }
  // Any other type: the rivals up to the first of a higher species number than the type.
  const auto upTo =
      std::ranges::find_if(data.rivals, [&](std::int32_t number) { return number > type; });
  const std::span<const std::int32_t> counted{data.rivals.begin(), upTo};
  return countSet(counted, rules.recruitTriggerBase, opening.trigger) >= winsNeeded;
}

Schedule buildSchedule(const ScheduleRules& rules, const TournamentTable& table,
                       const ScheduleFacts& facts) {
  Schedule schedule;
  schedule.firstDay = facts.day;
  for (std::size_t column = 0; column < ScheduleDays; ++column) {
    const auto day =
        static_cast<std::size_t>((facts.day + static_cast<std::int32_t>(column)) % rules.cycleDays);
    if (day >= table.days.size()) {
      continue;
    }
    std::size_t row = 0;
    for (const std::int32_t cup : table.days[day]) {
      if (row == ScheduleRows) {
        break;
      }
      if (facts.open(cup)) {
        schedule.days.at(column).at(row++) =
            ScheduleCell{.cup = cup, .eligible = false, .overlevel = false};
      }
    }
  }
  // Today's cups can be entered up to the last minute, tomorrow's all day.
  const std::size_t first = facts.minuteOfDay <= rules.lastMinuteToday ? 0 : 1;
  for (std::size_t column = first; column < 2; ++column) {
    for (std::optional<ScheduleCell>& cell : schedule.days.at(column)) {
      if (!cell) {
        break;
      }
      cell->eligible = facts.eligible(cell->cup);
      cell->overlevel = facts.overlevel(cell->cup);
    }
  }
  return schedule;
}

std::int32_t entryDay(const ScheduleRules& rules, std::int32_t day, std::size_t column) {
  return column == 0 ? day : (day + 1) | rules.tomorrowFlag;
}

} // namespace dwe::game::tournament
