#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/story/story_state.hpp>

#include <cstdint>
#include <vector>

// What a new day does to the story's variables (script_common.c: dailyPStatTrigger). The original
// runs it at each midnight, when a sleep or a training session crosses one, when a script changes
// the day, and once when a new game starts (script_interp.c: initializeScripts).
namespace dwe::game::story {

// A variable whose bits a new day clears: the scripts use them as once-a-day marks.
struct DailyMarks {
  std::int32_t variable = 0;
  std::uint8_t clearedBits = 0;
};

struct NewDayRules {
  // Variables that count the days: each grows by one a day, unless it holds `stoppedValue`; the
  // marks' variables are left alone then too.
  std::vector<std::int32_t> dayCounters;
  std::uint8_t stoppedValue = 0;
  std::vector<DailyMarks> dailyMarks;
};

// Runs a new day on the story's variables; a variable outside the story is an error and changes
// nothing after it.
core::Result<void> startNewDay(StoryState& state, const NewDayRules& rules);

} // namespace dwe::game::story
