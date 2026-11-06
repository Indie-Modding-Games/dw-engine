#pragma once

#include <dwe/screens/field_hud_view.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace dwe::game::battle {
class Battle;
} // namespace dwe::game::battle

namespace dwe::screens {

// The countdown over the fallen partner (command_menu.c: BTL_initializeDeathCountdown,
// BTL_addDeathCountdown, BTL_tickDeathCountdown, BTL_renderDeathCountdown): a star badge with a big
// digit on it, 5 to 1, a second each. DeathCountdownSetting packs its seven values, empty while it
// does not show: the anchor's x and y in percent of the window, the digit, the digit's and the
// badge's scale (1 as drawn), their turn in degrees and the badge's palette (0 to 2). It goes with
// the HP bars' revision (HpBarsRevisionSetting).
inline constexpr std::string_view DeathCountdownSetting = "battle.death_countdown";

// How the countdown looks on one of its frames.
struct DeathCountdownLook {
  // The digit shown, 5 down to 1.
  std::int32_t digit = 0;
  double digitScale = 1.0;
  double badgeScale = 1.0;
  // Clockwise, in degrees; it grows past a turn.
  double turn = 0.0;
  // The badge's three palettes take turns (BI_IN palettes 13, 14 and 15).
  std::int32_t badgePalette = 0;

  friend bool operator==(const DeathCountdownLook&, const DeathCountdownLook&) = default;
};

// The countdown's frames: the timer counts the battle frames since it was added (the first tick is
// 1); it is drawn for the last time on frame 150 (BTL_renderDeathCountdown removes it once the
// timer reaches 0x96).
inline constexpr std::int32_t DeathCountdownLastFrame = 150;

// The look on the timer's frame, none outside 1 to DeathCountdownLastFrame. Each second-long cycle
// of 31 steps (the timer's multiples of 31 start one, the first runs from 1) pops the digit (its
// scale 1, then +0x199 a step on steps 2 to 4 and -0x199 on 5 to 7, of 0x1000), turns the badge's
// palette every third step up to step 20, and from step 21 shrinks both by 5 % a step and turns
// them 64 degrees a step. The digit is (timer + 29) / 30 frames in: 5, 4, 3, 2, then 1.
[[nodiscard]] std::optional<DeathCountdownLook> deathCountdownLookOf(std::int32_t timer);

// The timer of a battle's countdown, none while it does not show: it is added on the fallen
// partner's frame EndingRules::reviveCountdownShownAt (BTL_isBattleFinished at DEATH_COUNTDOWN 20)
// and ticks with the frames the partner lies there (no tick while a thrown item flies, as the
// fall's count). Reviving the partner takes it away (item.c: handleRestore,
// BTL_removeDeathCountdown).
[[nodiscard]] std::optional<std::int32_t> deathCountdownTimerOf(const game::battle::Battle& battle);

// Where the countdown stands: the fallen partner's model node 1 as it showed when the countdown was
// added (getEntityScreenPos(entity, 1)), held within 140 pixels of the original's screen's middle
// across and 100 up or down.
[[nodiscard]] WindowFraction deathCountdownAnchorOf(WindowFraction node, double aspect);

// The countdown as the settings the battle document reads.
[[nodiscard]] ScreenSettings settingsOf(const std::optional<DeathCountdownLook>& look,
                                        WindowFraction anchor);

} // namespace dwe::screens
