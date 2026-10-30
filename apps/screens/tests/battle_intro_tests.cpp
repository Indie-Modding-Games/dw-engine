#include <dwe/screens/battle_intro.hpp>

#include <doctest/doctest.h>

#include <cstdint>

using dwe::screens::BattleIntro;
using dwe::screens::IntroStep;

namespace {

// The frames of the banner until it bursts, calling playerBack on frame `playerBackAt` (0: never).
std::int32_t bannerFrames(std::int32_t playerBackAt) {
  BattleIntro intro(1);
  while (intro.current() == IntroStep::Anger) {
    intro.step();
  }
  std::int32_t frames = 0;
  while (intro.current() == IntroStep::Banner && frames < 1000) {
    ++frames;
    if (frames == playerBackAt) {
      intro.playerBack();
    }
    intro.step();
  }
  return frames;
}

} // namespace

TEST_CASE("the banner stays 200 frames unless the player is back, then until its pieces are in") {
  CHECK(bannerFrames(0) == 200);
  // Back early, the banner waits for its pieces only.
  const std::int32_t early = bannerFrames(16);
  CHECK(early < 200);
  CHECK(early >= 17);
  // Back late, two more frames.
  CHECK(bannerFrames(150) == 151);
}
