#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 6;

} // namespace

// Routine 6: a tournament of the arena (tournament.c:128 scriptStartTournament).
core::Result<void> registerTournamentRoutine(StoryScenesContext& scenes) {
  return registerScreenRoutine(scenes, Routine, StoryScreen::Tournament, true);
}

} // namespace dwe::session
