#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/session/calendar_adapter.hpp>
#include <dwe/session/story_adapter.hpp>

#include <limits>
#include <string>
#include <utility>

#include "calendar_messages.hpp"

namespace dwe::session {

namespace {

using assets::YamlReader;
using core::YamlNode;
using game::story::DailyMarks;
using game::story::NewDayRules;

constexpr std::string_view NewDayKey = "new_day";
constexpr std::string_view DayCountersKey = "day_counters";
constexpr std::string_view StoppedValueKey = "stopped_value";
constexpr std::string_view DailyMarksKey = "daily_marks";
constexpr std::string_view VariableKey = "variable";
constexpr std::string_view ClearedBitsKey = "cleared_bits";

std::uint8_t byte(YamlReader& reader, const YamlNode& parent, std::string_view key) {
  const YamlNode& node = reader.field(parent, key);
  const auto value = reader.integer<std::int32_t>(node);
  if (reader.ok() && (value < 0 || value > std::numeric_limits<std::uint8_t>::max())) {
    reader.fail(node, core::formatMessage(calendar_messages::NotAByte, key));
  }
  return static_cast<std::uint8_t>(value);
}

std::vector<DailyMarks> readMarks(YamlReader& reader, const YamlNode& parent) {
  std::vector<DailyMarks> marks;
  for (const YamlNode& entry :
       reader.sequence(reader.field(parent, DailyMarksKey), DailyMarksKey)) {
    if (!reader.expectKeys(entry, calendar_messages::MarksText, {VariableKey, ClearedBitsKey})) {
      return marks;
    }
    marks.push_back({.variable = reader.integer<std::int32_t>(reader.field(entry, VariableKey)),
                     .clearedBits = byte(reader, entry, ClearedBitsKey)});
  }
  return marks;
}

} // namespace

core::Result<NewDayRules> readCalendarRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, calendar_messages::SectionText, {NewDayKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  const YamlNode& newDay = reader.field(root, NewDayKey);
  if (!reader.expectKeys(newDay, calendar_messages::NewDayText,
                         {DayCountersKey, StoppedValueKey, DailyMarksKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  NewDayRules rules;
  for (const YamlNode& counter :
       reader.sequence(reader.field(newDay, DayCountersKey), DayCountersKey)) {
    rules.dayCounters.push_back(reader.integer<std::int32_t>(counter));
  }
  rules.stoppedValue = byte(reader, newDay, StoppedValueKey);
  rules.dailyMarks = readMarks(reader, newDay);
  return reader.finish(std::move(rules), section.file);
}

CalendarAdapter::CalendarAdapter(StoryAdapter& story) : story_{&story} {}

ruleset::SectionChecks CalendarAdapter::sectionChecks() const {
  return {{std::string{CalendarSection}, [](const ruleset::Section& section) -> core::Result<void> {
             auto rules = readCalendarRules(section);
             if (!rules) {
               return std::unexpected{rules.error()};
             }
             return {};
           }}};
}

core::Result<void> CalendarAdapter::readRules(const ruleset::Ruleset& rules) {
  // A ruleset without the section has days that change nothing in the story.
  const ruleset::Section* section = rules.section(CalendarSection);
  if (section == nullptr) {
    core::logDebug("[port:calendar] the ruleset has no calendar section: days change no variable");
    rules_ = NewDayRules{};
    return {};
  }
  auto read = readCalendarRules(*section);
  if (!read) {
    return std::unexpected{read.error()};
  }
  rules_ = std::move(*read);
  return {};
}

core::Result<void> CalendarAdapter::startNewGame(GameSession& /*session*/) {
  if (!rules_) {
    return core::fail(std::string{calendar_messages::NotRead});
  }
  core::logDebug("[port:calendar] initializeScripts (script_interp.c:2221): a new game's day");
  return game::story::startNewDay(story_->state(), *rules_);
}

void CalendarAdapter::routePartnerEvent(GameSession& /*session*/,
                                        const game::partner::PartnerEvent& event) {
  if (event.kind == game::partner::EventKind::NewDay) {
    startNewDays(event.value);
  }
}

void CalendarAdapter::startNewDays(std::int32_t days) {
  if (!rules_) {
    return;
  }
  core::logDebug("[port:calendar] tickGameClock (clock.c:148): {} new day(s)", days);
  for (std::int32_t day = 0; day < days; ++day) {
    if (auto started = game::story::startNewDay(story_->state(), *rules_); !started) {
      core::logWarning("{}",
                       core::formatMessage(calendar_messages::NewDayFailed,
                                           started.error().message));
      return;
    }
  }
}

} // namespace dwe::session
