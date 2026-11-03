#pragma once

#include <dwe/game/curling/curling.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <cstdint>

namespace dwe::app {

// The stones game's recruit bar, one of the recruit bars (recruit_bar.hpp, the option
// RecruitBarSetting): during the match, a line under the original's HUD shows the player's points
// against the opponent's, the throws each has left and how the story would answer the match if it
// ended now. The story (script 206, section 0x0a) reads the result KAR_finishMatch writes: any win
// makes the opponent join the city; a win of perfectScore or more is the perfect score, which also
// gives a medal. Only the first opponent joins, so the bar shows only in its match. It changes no
// rule: it reads the match.
struct CurlingRecruitView {
  bool shown = false;
  std::int32_t player = 0;
  std::int32_t opponent = 0;
  // The throws each thrower has left.
  std::int32_t playerThrows = 0;
  std::int32_t opponentThrows = 0;
  // The perfect score can still be reached: the player's stones still in the game (in the rack, in
  // the hand or on the ice) could each rest on the best ring.
  bool perfectReachable = false;

  friend bool operator==(const CurlingRecruitView&, const CurlingRecruitView&) = default;
};

// What the bar shows for `match` (nullptr: no match), hidden with the option off.
[[nodiscard]] CurlingRecruitView curlingRecruitOf(const game::curling::Match* match,
                                                  const game::curling::CurlingRules& rules,
                                                  bool enabled);

[[nodiscard]] screens::ScreenSettings settingsOf(const CurlingRecruitView& view);

} // namespace dwe::app
