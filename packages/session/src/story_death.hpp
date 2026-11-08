#pragma once

#include <cstdint>

// The death the story plays (StoryScenes, rules.death): when a partner event asks for the death
// section, and when a rebirth waits for the egg box.
namespace dwe::session {

// call_routine 29: the egg box (doo2.c: DOO2_openEggBox).
inline constexpr std::int64_t EggBoxRoutine = 29;

// A lost life or a death routed to the story asks for the death section (MAPHEAD 0x4DE) unless
// that section already ran for it (`dying`). partner_impl.c: tickDeathCondition calls the section
// on the frame the partner dies; here the dead partner asks on that frame and its Died event is
// routed after, which must not ask again once the partner is reborn.
[[nodiscard]] constexpr bool deathEventAsksForSection(bool dying) {
  return !dying;
}

// The rebirth of a partner with no lives left (script 212, section 0x49, call_partner_routine 4)
// opens the egg box first when the rules let its egg count (life.rebirth.random_egg_without_lives
// off, the option choose_egg_after_lives_lost) and no egg was taken yet. The original never opens
// it there: DOOA_getSequenceState draws the egg (rand() % 4).
[[nodiscard]] constexpr bool rebirthOpensEggBox(std::int32_t lives, bool randomEggWithoutLives,
                                                bool eggTaken) {
  return lives <= 0 && !randomEggWithoutLives && !eggTaken;
}

} // namespace dwe::session
