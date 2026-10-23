#include <dwe/core/log.hpp>
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/session/game_session.hpp>

#include <array>
#include <cstdint>

#include "story_game_context.hpp"
#include "story_game_messages.hpp"

// The story's orders of the date (script_interp.c: scriptInstruction10to27 STORE_DATE,
// scriptInstruction28to3F ADVANCE_TO_DATE_AT and ADD_MINUTES_TO_DATE_AT) and its daily change
// (script_common.c: dailyPStatTrigger). A date in variables is four in a row: year, day, hour and
// minute.
namespace dwe::session {

namespace {

using script::ApiValue;

constexpr std::int64_t DateVariables = 4;
// A variable holding this is not counted by the daily change (script_common.c:1427).
constexpr std::int64_t UncountedValue = 255;
// math.c: randomLimit(limit) is (limit * rand()) >> 15, rand() being the session's high bits.
constexpr std::uint32_t RandDroppedBits = 17;
constexpr int RandomBits = 15;

std::int64_t minutesPerDay() {
  return ruleset::HoursPerDay * ruleset::MinutesPerHour;
}

// script_interp.c:2512 dateToSeconds counts minutes, with the calendar's days per year.
std::int64_t minutesOf(const ruleset::GameTime& date, const ruleset::ClockRules& clock) {
  const std::int64_t days = (std::int64_t{date.year} * clock.daysPerYear) + date.day;
  const std::int64_t hours = (days * ruleset::HoursPerDay) + date.hour;
  return (hours * ruleset::MinutesPerHour) + date.minute;
}

ruleset::GameTime dateOf(std::int64_t minutes, const ruleset::ClockRules& clock) {
  const std::int64_t days = minutes / minutesPerDay();
  return {.year = static_cast<std::int32_t>(days / clock.daysPerYear),
          .day = static_cast<std::int32_t>(days % clock.daysPerYear),
          .hour =
              static_cast<std::int32_t>((minutes / ruleset::MinutesPerHour) % ruleset::HoursPerDay),
          .minute = static_cast<std::int32_t>(minutes % ruleset::MinutesPerHour)};
}

core::Result<ruleset::GameTime> readDate(const StoryGameContext& context, std::int64_t first) {
  std::array<std::int32_t, DateVariables> parts{};
  for (std::int64_t part = 0; part < DateVariables; ++part) {
    const auto value = context.variable(first + part);
    if (!value) {
      return std::unexpected{value.error()};
    }
    parts.at(static_cast<std::size_t>(part)) = static_cast<std::int32_t>(*value);
  }
  return ruleset::GameTime{.year = parts[0], .day = parts[1], .hour = parts[2], .minute = parts[3]};
}

core::Result<void> writeDate(const StoryGameContext& context, std::int64_t first,
                             const ruleset::GameTime& date) {
  const std::array<std::int64_t, DateVariables> parts{date.year, date.day, date.hour, date.minute};
  for (std::int64_t part = 0; part < DateVariables; ++part) {
    if (auto written = context.setVariable(first + part, parts.at(static_cast<std::size_t>(part)));
        !written) {
      return written;
    }
  }
  return {};
}

core::Result<ApiValue> storeDate(const StoryGameContext& context, std::int64_t first) {
  const ruleset::GameTime now = context.game().clock().time();
  core::logDebug("[port:story] store_date (script_interp.c:825): year {} day {} {:02}:{:02} in {}",
                 now.year, now.day, now.hour, now.minute, first);
  return done(writeDate(context, first, now));
}

core::Result<ApiValue> advanceToDate(const StoryGameContext& context, std::int64_t first) {
  const auto target = readDate(context, first);
  if (!target) {
    return std::unexpected{target.error()};
  }
  GameSession& session = context.game();
  ruleset::GameClock& clock = session.clock();
  const ruleset::GameTime now = clock.time();
  const std::int64_t targetMinutes = minutesOf(*target, clock.rules());
  const std::int64_t nowMinutes = minutesOf(now, clock.rules());
  // script_interp.c:1236: a later date first passes its whole hours on the partner; an earlier one
  // only sets the clock.
  if (nowMinutes < targetMinutes) {
    const auto hours =
        static_cast<std::int32_t>((targetMinutes - nowMinutes) / ruleset::MinutesPerHour);
    core::logDebug("[port:story] advance_to_date_at (script_interp.c:1238): skipHours({})", hours);
    session.partner().skipHours(hours, clock);
  }
  core::logDebug("[port:story] advance_to_date_at (script_interp.c:1242): year {} day {} "
                 "{:02}:{:02} -> year {} day {} {:02}:{:02}",
                 now.year, now.day, now.hour, now.minute, target->year, target->day, target->hour,
                 target->minute);
  clock.restore(targetMinutes * clock.rules().framesPerMinute, 0.0);
  if (target->day != now.day) {
    runDailyChange(context);
  }
  return ApiValue{};
}

core::Result<ApiValue> addMinutesToDate(const StoryGameContext& context, std::int64_t first,
                                        std::int64_t minutes) {
  const auto date = readDate(context, first);
  if (!date) {
    return std::unexpected{date.error()};
  }
  const ruleset::ClockRules& clock = context.game().clock().rules();
  const std::int64_t total = minutesOf(*date, clock) + minutes;
  const std::int64_t limit =
      std::int64_t{context.rules.dateYearLimit} * clock.daysPerYear * minutesPerDay();
  // script_interp.c:2554 secondsToDate: past the limit (or below 0, which it reads unsigned) the
  // date is a fixed one.
  const bool overflows = total < 0 || total >= limit;
  const ruleset::GameTime result = overflows ? context.rules.dateOverflow : dateOf(total, clock);
  core::logDebug("[port:story] add_minutes_to_date_at (script_interp.c:1252): {} minutes to "
                 "variable {}: year {} day {} {:02}:{:02}{}",
                 minutes, first, result.year, result.day, result.hour, result.minute,
                 overflows ? " (past the limit)" : "");
  return done(writeDate(context, first, result));
}

std::int64_t drawBelow(GameSession& session, std::int64_t limit) {
  const auto drawn = std::int64_t{session.random().next() >> RandDroppedBits};
  return (limit * drawn) >> RandomBits;
}

void writeDaily(const StoryGameContext& context, std::int64_t variable, std::int64_t value) {
  if (auto written = context.setVariable(variable, value); !written) {
    core::logWarning("{}",
                     story_game_messages::formatMessage(story_game_messages::VariableNotWritten,
                                                        variable, written.error().message));
  }
}

} // namespace

void runDailyChange(const StoryGameContext& context) {
  // script_common.c:1399 dailyPStatTrigger.
  const StoryDailyRules& daily = context.rules.daily;
  GameSession& session = context.game();
  for (std::int32_t draw = 0; draw < daily.draws; ++draw) {
    std::int64_t number = 0;
    do {
      number = drawBelow(session, daily.drawRange) + 1;
    } while (number == daily.drawSkipped);
    // Where the original keeps these (ScriptState) nothing reads them in the story yet.
    core::logDebug("[port:story] dailyPStatTrigger (script_common.c:1413): daily number {} = {}",
                   draw, number);
  }
  for (const std::int32_t counter : daily.counterVariables) {
    const auto value = context.variable(counter);
    if (value && *value != UncountedValue) {
      writeDaily(context, counter, *value + 1);
    }
  }
  const auto masked = context.variable(daily.maskedVariable);
  if (masked && *masked != UncountedValue) {
    writeDaily(context, daily.maskedVariable, *masked & daily.mask);
  }
}

core::Result<void> publishDateOrders(script::ApiRegistry& api, StoryGameContext& context) {
  namespace names = story_game_api;
  const StoryGameContext* game = &context;
  using Operands1 = std::array<std::int64_t, 1>;
  using Operands2 = std::array<std::int64_t, 2>;
  return publishOrders(
      api, {{names::StoreDate, integerOrder<1>([game](const Operands1& operands) {
               return storeDate(*game, operands[0]);
             })},
            {names::AdvanceToDateAt, integerOrder<1>([game](const Operands1& operands) {
               return advanceToDate(*game, operands[0]);
             })},
            {names::AddMinutesToDateAt, integerOrder<2>([game](const Operands2& operands) {
               return addMinutesToDate(*game, operands[0], operands[1]);
             })}});
}

} // namespace dwe::session
