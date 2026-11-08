// The death the story plays (story_death.hpp): when a routed partner event asks for the death
// section, and when the rebirth with no lives left opens the egg box first.

#include <doctest/doctest.h>

#include "packages/session/src/story_death.hpp"

namespace {

using dwe::session::deathEventAsksForSection;
using dwe::session::rebirthOpensEggBox;

} // namespace

TEST_CASE("a death routed after its section was asked for does not ask for it again") {
  // tickDeathCondition calls MAPHEAD 0x4DE once; the Died event routed a frame later used to ask
  // again and play a lost life on the reborn partner.
  CHECK(deathEventAsksForSection(false));
  CHECK_FALSE(deathEventAsksForSection(true));
}

TEST_CASE("only a rebirth with no lives left and the random egg off opens the egg box first") {
  constexpr bool RandomEgg = true;
  constexpr bool EggTaken = true;
  // The original: no lives left draws the egg (DOOA_getSequenceState), no box.
  CHECK_FALSE(rebirthOpensEggBox(0, RandomEgg, !EggTaken));
  // choose_egg_after_lives_lost: the box opens, once.
  CHECK(rebirthOpensEggBox(0, !RandomEgg, !EggTaken));
  CHECK_FALSE(rebirthOpensEggBox(0, !RandomEgg, EggTaken));
  // A natural death's section opened the box itself (routine 29).
  CHECK_FALSE(rebirthOpensEggBox(2, !RandomEgg, !EggTaken));
}
