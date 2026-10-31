#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 10;

} // namespace

// Routine 10: the bird transport (shop/script_menu.c:987 tickBirdraTransport); the carrier is the
// NPC variable 254 names. The script waits for the screen, and goes on at the MAPHEAD section the
// yes leads to (session::BirdTransport::travel) or, without a trip, after the call.
core::Result<void> registerBirdTransportRoutine(StoryScenesContext& scenes) {
  return registerScreenRoutine(scenes, Routine, StoryScreen::BirdTransport, true);
}

} // namespace dwe::session
