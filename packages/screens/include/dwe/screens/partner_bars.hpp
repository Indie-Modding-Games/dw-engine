#pragma once

#include <dwe/screens/screen_settings.hpp>

#include <cstdint>
#include <string_view>

namespace dwe::screens {

// The settings of the partner's bars' slide on the battle document, published with the command
// strip's (CommandMenuRevisionSetting): the left edge of the HP and of the MP bar in the original's
// screen pixels, 0 at the middle of its 320 (160, the right edge of the screen, before they come in
// and once they have gone).
inline constexpr std::string_view PartnerHpBarXSetting = "battle.bars.hp_x";
inline constexpr std::string_view PartnerMpBarXSetting = "battle.bars.mp_x";

// The slide of the partner's HP and MP bars (battle_hud.c: BTL_initializePartnerStatusBars,
// BTL_tickPartnerStatusBars). While the command strip is up (BTL_COMMAND_MENU_ACTIVE 1) the bars
// come in from the right edge of the screen: the HP bar in 7 frames, the MP bar starting on the
// 3rd and in place on the 9th. Once the strip goes back up after the fight they leave the same way,
// the MP bar first and the HP bar from the 3rd frame on, gone on the 9th. Until then they stay,
// through the victory and the end box. Frame by frame at the battle's rate.
class PartnerBarsMotion {
public:
  // BTL_initializePartnerStatusBars: every step at 0, off the right edge.
  void reset();
  // BTL_tickPartnerStatusBars: one battle frame; `stripUp` is BTL_COMMAND_MENU_ACTIVE == 1.
  void step(bool stripUp);

  // The bars' left edges (BTL_STATUS_BAR_X of their steps).
  [[nodiscard]] std::int32_t hpX() const;
  [[nodiscard]] std::int32_t mpX() const;

  [[nodiscard]] ScreenSettings settings() const;

private:
  // BTL_STATUS_BARS_STEP, BTL_HP_BAR_STEP and BTL_MP_BAR_STEP, 0 to 7.
  std::int32_t barsStep_ = 0;
  std::int32_t hpStep_ = 0;
  std::int32_t mpStep_ = 0;
};

} // namespace dwe::screens
