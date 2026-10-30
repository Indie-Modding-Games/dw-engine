#include "sleep_steps.hpp"

#include <dwe/core/log.hpp>

#include <optional>

namespace dwe::app {
namespace {

std::uint32_t clipId(std::int32_t clip) {
  return static_cast<std::uint32_t>(clip);
}

} // namespace

SleepSteps::Order SleepSteps::start() {
  // partner.c:296-300: beside the player, playSound(0, 15) (the caller's), the player's clip and
  // the partner standing.
  step_ = Step::PlayerClip;
  return {.playerClip = clipId(scene_.playerClip),
          .partnerClip = clipId(scene_.partnerIdleClip),
          .partnerMoves = false,
          .fadeOut = false};
}

SleepSteps::Order SleepSteps::tick(const Seen& seen) {
  switch (step_) {
  case Step::PlayerClip:
    // partner.c:304: once the player's clip ends, the partner yawns.
    if (seen.playerPlayedThrough) {
      step_ = Step::PartnerYawn;
      core::logDebug("[port:sleep] partnerTickSleep (partner.c:306): the partner yawns");
      return {.playerClip = std::nullopt,
              .partnerClip = clipId(scene_.partnerYawnClip),
              .partnerMoves = false,
              .fadeOut = false};
    }
    break;
  case Step::PartnerYawn:
    // partner.c:311: once the yawn ends, it lies down.
    if (seen.partnerFramesLeft <= 0.0) {
      step_ = Step::PartnerLieDown;
      core::logDebug("[port:sleep] partnerTickSleep (partner.c:313): the partner lies down");
      return {.playerClip = std::nullopt,
              .partnerClip = clipId(scene_.partnerLieDownClip),
              .partnerMoves = true,
              .fadeOut = false};
    }
    break;
  case Step::PartnerLieDown:
    // partner.c:318: the fade out starts that many frames before the lie-down ends.
    if (seen.partnerFramesLeft <= static_cast<double>(scene_.fadeLeadFrames)) {
      step_ = Step::None;
      core::logDebug("[port:sleep] partnerTickSleep (partner.c:320): fadeToBlack");
      return {.playerClip = std::nullopt,
              .partnerClip = std::nullopt,
              .partnerMoves = false,
              .fadeOut = true};
    }
    break;
  case Step::None:
    break;
  }
  return {};
}

SleepSteps::Order SleepSteps::wakeUp() {
  // partner.c:349: the fade in shows the partner standing; the player keeps its clip's last pose
  // until it gets control back.
  step_ = Step::None;
  return {.playerClip = std::nullopt,
          .partnerClip = clipId(scene_.partnerIdleClip),
          .partnerMoves = false,
          .fadeOut = false};
}

} // namespace dwe::app
