#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber LibraryRoutine = 39;
constexpr game::story::RoutineNumber CourseRoutine = 7;

} // namespace

// Routine 39 (map.c:5457 loadTrainingLibrary) only clears TRAINING_COMPLETE and goes on; routine 7
// runs the course pstat 254 picks and tickScript waits until isTrainingComplete
// (script_interp.c:459).
core::Result<void> registerGymRoutines(StoryScenesContext& scenes) {
  if (auto library = registerScreenRoutine(scenes, LibraryRoutine, StoryScreen::GymLibrary, false);
      !library) {
    return library;
  }
  return registerScreenRoutine(scenes, CourseRoutine, StoryScreen::GymCourse, true);
}

} // namespace dwe::session
