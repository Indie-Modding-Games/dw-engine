#include "../story_scenes_context.hpp"
#include "routines.hpp"

namespace dwe::session {

namespace {

constexpr game::story::RoutineNumber Routine = 1;

} // namespace

// Routine 1: the shop that sells for merit points (shop/script_menu.c:788 openMeritShop).
core::Result<void> registerMeritShopRoutine(StoryScenesContext& scenes) {
  return registerScreenRoutine(scenes, Routine, StoryScreen::MeritShop, true);
}

} // namespace dwe::session
