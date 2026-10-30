#pragma once

#include <dwe/game/partner/care_rules.hpp>

#include <cstdint>
#include <optional>

namespace dwe::app {

// The way to a sleep once the partner is beside the player (partner.c: partnerTickSleep, sub states
// 1 to 4): the player plays its clip while the partner stands, then the partner yawns and lies
// down, and the screen fades out a little before the lie-down ends. The field's clips are what it
// reads and what it orders; SleepScreen plays them.
class SleepSteps {
public:
  // What the field shows on a tick.
  struct Seen {
    // Whether the player's clip has played through once (anim.c: animFrame reaching frameCount).
    bool playerPlayedThrough = false;
    // Frames of the original left in the partner's clip (frameCount - animFrame); 0 or less once it
    // has ended, and for a clip the model lacks.
    double partnerFramesLeft = 0.0;
  };

  // What to change on the field.
  struct Order {
    std::optional<std::uint32_t> playerClip;
    std::optional<std::uint32_t> partnerClip;
    // Whether the partner's clip carries it over the ground by its root motion (the lie-down).
    bool partnerMoves = false;
    bool fadeOut = false;
  };

  explicit SleepSteps(const game::partner::SleepScene& scene) : scene_(scene) {}

  // Sub state 1 to 2: the partner has come beside the player.
  [[nodiscard]] Order start();
  // Sub states 2 to 4, once per tick; the order that fades the screen out ends the steps.
  [[nodiscard]] Order tick(const Seen& seen);
  // Sub state 6: the fade in wakes the partner up standing.
  [[nodiscard]] Order wakeUp();
  [[nodiscard]] bool running() const { return step_ != Step::None; }

private:
  enum class Step : std::uint8_t { None, PlayerClip, PartnerYawn, PartnerLieDown };

  game::partner::SleepScene scene_;
  Step step_ = Step::None;
};

} // namespace dwe::app
