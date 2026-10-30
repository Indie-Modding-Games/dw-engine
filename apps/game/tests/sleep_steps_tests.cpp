#include <dwe/game/partner/care_rules.hpp>

#include <doctest/doctest.h>

#include <limits>

#include "sleep_steps.hpp"

using dwe::app::SleepSteps;

namespace {

// care.yaml's sleep scene (partner.c: partnerTickSleep).
constexpr dwe::game::partner::SleepScene Scene{.playerClip = 8,
                                               .partnerIdleClip = 0,
                                               .partnerYawnClip = 11,
                                               .partnerLieDownClip = 9,
                                               .fadeLeadFrames = 5};
constexpr double NotStarted = std::numeric_limits<double>::infinity();

} // namespace

TEST_CASE("sleep steps: the player's clip, the yawn, the lie-down, then the fade, as "
          "partnerTickSleep") {
  SleepSteps steps{Scene};
  CHECK_FALSE(steps.running());

  // Sub state 1 -> 2: beside the player, the player's clip and the partner standing.
  const SleepSteps::Order start = steps.start();
  CHECK(start.playerClip == 8U);
  CHECK(start.partnerClip == 0U);
  CHECK_FALSE(start.fadeOut);
  CHECK(steps.running());

  // Sub state 2: nothing until the player's clip has played through, however the partner's goes.
  for (int tick = 0; tick < 10; ++tick) {
    const SleepSteps::Order wait =
        steps.tick({.playerPlayedThrough = false, .partnerFramesLeft = 0.0});
    CHECK_FALSE(wait.partnerClip.has_value());
    CHECK_FALSE(wait.fadeOut);
  }
  const SleepSteps::Order yawn =
      steps.tick({.playerPlayedThrough = true, .partnerFramesLeft = NotStarted});
  CHECK(yawn.partnerClip == 11U);
  CHECK_FALSE(yawn.partnerMoves);
  CHECK_FALSE(yawn.playerClip.has_value());

  // Sub state 3: the yawn waits for its clip to start and to end.
  CHECK_FALSE(steps.tick({.playerPlayedThrough = true, .partnerFramesLeft = NotStarted})
                  .partnerClip.has_value());
  CHECK_FALSE(
      steps.tick({.playerPlayedThrough = true, .partnerFramesLeft = 0.5}).partnerClip.has_value());
  const SleepSteps::Order lieDown =
      steps.tick({.playerPlayedThrough = true, .partnerFramesLeft = 0.0});
  CHECK(lieDown.partnerClip == 9U);
  // anim.c: applyRootMomentum: the lie-down carries the partner toward the player.
  CHECK(lieDown.partnerMoves);
  CHECK_FALSE(lieDown.fadeOut);

  // Sub state 4: the fade starts 5 frames before the lie-down ends (animFrame >= frameCount - 5).
  CHECK_FALSE(steps.tick({.playerPlayedThrough = true, .partnerFramesLeft = NotStarted}).fadeOut);
  CHECK_FALSE(steps.tick({.playerPlayedThrough = true, .partnerFramesLeft = 5.5}).fadeOut);
  const SleepSteps::Order fade =
      steps.tick({.playerPlayedThrough = true, .partnerFramesLeft = 5.0});
  CHECK(fade.fadeOut);
  CHECK_FALSE(fade.partnerClip.has_value());
  CHECK_FALSE(steps.running());
  // Once faded, the steps order nothing more.
  CHECK_FALSE(steps.tick({.playerPlayedThrough = true, .partnerFramesLeft = 0.0}).fadeOut);

  // Sub state 6: the fade in shows the partner standing.
  const SleepSteps::Order wake = steps.wakeUp();
  CHECK(wake.partnerClip == 0U);
  CHECK_FALSE(wake.playerClip.has_value());
}
