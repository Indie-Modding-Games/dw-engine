#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/story/new_day.hpp>
#include <dwe/session/session_system.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace dwe::session {

class StoryAdapter;

// The ruleset section of the calendar.
inline constexpr std::string_view CalendarSection = "calendar";

// Reads the calendar section; errors name the file and line.
core::Result<game::story::NewDayRules> readCalendarRules(const ruleset::Section& section);

// The calendar's days (clock.c, script_common.c: dailyPStatTrigger): the game clock counts the
// days and the partner reports each midnight crossed (NewDay); this runs what a new day does to the
// story. The year's rules are the partner's (YearRules).
class CalendarAdapter final : public SessionSystem {
public:
  // The story must outlive the adapter.
  explicit CalendarAdapter(StoryAdapter& story);

  [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
  // Reads the calendar section; without one, a day changes nothing in the story.
  core::Result<void> readRules(const ruleset::Ruleset& rules) override;

  // A new game runs one new day on the story, as script_interp.c: initializeScripts does; it must
  // tick after the story's own start.
  core::Result<void> startNewGame(GameSession& session) override;

  // NewDay: one new day per midnight crossed.
  void routePartnerEvent(GameSession& session, const game::partner::PartnerEvent& event) override;

  // Runs `days` new days on the story, as a script that changes the date does.
  void startNewDays(std::int32_t days);

private:
  StoryAdapter* story_;
  std::optional<game::story::NewDayRules> rules_;
};

} // namespace dwe::session
