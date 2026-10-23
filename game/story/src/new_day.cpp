#include <dwe/core/log.hpp>
#include <dwe/game/story/new_day.hpp>

namespace dwe::game::story {

namespace {

// dailyPStatTrigger: a counter that holds the stopped value stays; any other grows, up to the
// stopped value.
core::Result<void> countDay(StoryState& state, std::int32_t variable, std::uint8_t stopped) {
  auto value = state.variable(variable);
  if (!value) {
    return std::unexpected{value.error()};
  }
  if (*value == stopped) {
    core::logDebug("[port:calendar] dailyPStatTrigger (script_common.c:1425): var {} stopped",
                   variable);
    return {};
  }
  const auto next = static_cast<std::uint8_t>(*value + 1);
  core::logDebug("[port:calendar] dailyPStatTrigger (script_common.c:1425): var {} {} -> {}",
                 variable, *value, next);
  return state.setVariable(variable, next);
}

// dailyPStatTrigger leaves the marks alone while their variable holds the stopped value too.
core::Result<void> clearMarks(StoryState& state, const DailyMarks& marks, std::uint8_t stopped) {
  auto value = state.variable(marks.variable);
  if (!value) {
    return std::unexpected{value.error()};
  }
  if (*value == stopped) {
    return {};
  }
  const auto cleared = static_cast<std::uint8_t>(*value & ~marks.clearedBits);
  core::logDebug("[port:calendar] dailyPStatTrigger (script_common.c:1432): var {} {} -> {}",
                 marks.variable, *value, cleared);
  return state.setVariable(marks.variable, cleared);
}

} // namespace

core::Result<void> startNewDay(StoryState& state, const NewDayRules& rules) {
  for (const std::int32_t counter : rules.dayCounters) {
    if (auto counted = countDay(state, counter, rules.stoppedValue); !counted) {
      return counted;
    }
  }
  for (const DailyMarks& marks : rules.dailyMarks) {
    if (auto cleared = clearMarks(state, marks, rules.stoppedValue); !cleared) {
      return cleared;
    }
  }
  return {};
}

} // namespace dwe::game::story
