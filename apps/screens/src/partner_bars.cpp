#include <dwe/screens/partner_bars.hpp>

#include <array>
#include <cstddef>
#include <string>

namespace dwe::screens {
namespace {

// battle_effect.c: BTL_STATUS_BAR_X, a bar's left edge at each step.
constexpr std::array<std::int32_t, 8> SlideX{160, 129, 104, 83, 66, 55, 48, 46};
constexpr std::int32_t LastStep = 7;
// BTL_tickPartnerStatusBars: the MP bar comes in from the 3rd step of the slide, and the HP bar
// leaves once the slide is under its 5th.
constexpr std::int32_t MpComesInFrom = 3;
constexpr std::int32_t HpLeavesUnder = 5;

std::int32_t slideX(std::int32_t step) {
  return SlideX.at(static_cast<std::size_t>(step));
}

} // namespace

void PartnerBarsMotion::reset() {
  barsStep_ = 0;
  hpStep_ = 0;
  mpStep_ = 0;
}

void PartnerBarsMotion::step(bool stripUp) {
  if (!stripUp) {
    if (barsStep_ != 0) {
      --barsStep_;
    }
    if (mpStep_ != 0) {
      --mpStep_;
    }
    if (barsStep_ < HpLeavesUnder && hpStep_ != 0) {
      --hpStep_;
    }
    return;
  }
  if (barsStep_ < LastStep) {
    ++barsStep_;
  }
  if (barsStep_ != 0 && hpStep_ < LastStep) {
    ++hpStep_;
  }
  if (barsStep_ >= MpComesInFrom && mpStep_ < LastStep) {
    ++mpStep_;
  }
}

std::int32_t PartnerBarsMotion::hpX() const {
  return slideX(hpStep_);
}

std::int32_t PartnerBarsMotion::mpX() const {
  return slideX(mpStep_);
}

ScreenSettings PartnerBarsMotion::settings() const {
  return {
      {.name = std::string{PartnerHpBarXSetting}, .value = static_cast<double>(hpX())},
      {.name = std::string{PartnerMpBarXSetting}, .value = static_cast<double>(mpX())},
  };
}

} // namespace dwe::screens
