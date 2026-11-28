#include <doctest/doctest.h>

#include "field_scene.hpp"

using dwe::app::FieldStage;

// The isOnMap tables measured in the original (dw-original, 2026-11-04) at each step of the
// evolution and the death scenes, as the stages the scenes set.

TEST_CASE("the evolution leaves only the partner drawn and then everyone again") {
  // EVL_setOtherEntitiesVisible(0) at frame 0x20 of state 0 (twna01): the player and the two
  // NPCs off the map, the partner on.
  FieldStage hidden;
  hidden.sceneryHidden = true;
  CHECK_FALSE(hidden.showsPlayer());
  CHECK_FALSE(hidden.showsNpcs());
  CHECK(hidden.showsPartner());
  // EVL_setOtherEntitiesVisible(1) at state 3: all back.
  const FieldStage restored;
  CHECK(restored.showsPlayer());
  CHECK(restored.showsNpcs());
  CHECK(restored.showsPartner());
}

TEST_CASE("the death hides everyone, keeps the wake map's host and brings all back with the egg") {
  // DOOA_setOtherEntitiesVisible(0) and DOOA_hideAllButPartner, then the partner vanishes and
  // ENTITY_TABLE[2] (the host) comes on alone (room08: only entity 2 on the map).
  FieldStage dissolve;
  dissolve.sceneryHidden = true;
  dissolve.partnerHidden = true;
  dissolve.shownNpc = static_cast<dwe::scene::Entity>(2);
  CHECK_FALSE(dissolve.showsPlayer());
  CHECK_FALSE(dissolve.showsNpcs());
  CHECK_FALSE(dissolve.showsPartner());
  // Frame 152 of the rebirth: DOOA_setOtherEntitiesVisible(1), DOOA_showPlayerAndPartner, then
  // the partner off for the egg: the player and entities 2 to 4 on, the partner off.
  FieldStage swap;
  swap.partnerHidden = true;
  CHECK(swap.showsPlayer());
  CHECK(swap.showsNpcs());
  CHECK_FALSE(swap.showsPartner());
}

TEST_CASE("a scene of this player's own hides the other co-op players and their partners") {
  // Co-op QA: the other players' partners stood beside this player's partner in its death on
  // black, its evolution and the lives box.
  FieldStage scene;
  scene.sceneryHidden = true;
  CHECK_FALSE(scene.showsOtherPlayers());
  CHECK(scene.showsPartner());
  CHECK(FieldStage{}.showsOtherPlayers());
  FieldStage hiddenPartner;
  hiddenPartner.partnerHidden = true;
  CHECK(hiddenPartner.showsOtherPlayers());
}

TEST_CASE("a co-op game turns to the world's time of day even past the hour it starts") {
  // Co-op QA: guests whose clock jumped to the host's in a dialog drew night while the host, held
  // across the dusk's first hour, drew day, in the same battle.
  using dwe::render::TimeOfDay;
  const dwe::render::TimeOfDaySchedule schedule{.dayStart = 6, .duskStart = 16, .nightStart = 20};
  using dwe::app::timeOfDayTurn;
  // The original: only the first hour of a time of day starts its fade.
  CHECK(timeOfDayTurn(16, TimeOfDay::Day, schedule, false) == TimeOfDay::Dusk);
  CHECK_FALSE(timeOfDayTurn(18, TimeOfDay::Day, schedule, false));
  CHECK_FALSE(timeOfDayTurn(17, TimeOfDay::Dusk, schedule, false));
  // Co-op: any hour whose time of day is not the one shown.
  CHECK(timeOfDayTurn(18, TimeOfDay::Day, schedule, true) == TimeOfDay::Dusk);
  CHECK(timeOfDayTurn(22, TimeOfDay::Day, schedule, true) == TimeOfDay::Night);
  CHECK(timeOfDayTurn(9, TimeOfDay::Night, schedule, true) == TimeOfDay::Day);
  CHECK_FALSE(timeOfDayTurn(18, TimeOfDay::Dusk, schedule, true));
}
