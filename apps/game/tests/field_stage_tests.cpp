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
