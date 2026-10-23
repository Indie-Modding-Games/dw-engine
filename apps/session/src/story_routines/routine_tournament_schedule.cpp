#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 18;

} // namespace

// Routine 18: the tournaments' calendar (dget/dget.c:664 initTournamentSchedule).
core::Result<void> registerTournamentScheduleRoutine(StoryScenesContext& scenes) {
  return registerScreenRoutine(scenes, Routine, StoryScreen::TournamentSchedule, true);
}

} // namespace dwe::session
