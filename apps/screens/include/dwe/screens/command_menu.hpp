#pragma once

#include <dwe/screens/screen_settings.hpp>

#include <cstdint>
#include <string_view>

namespace dwe::screens {

// The settings of the command strip's motion on the battle document, which redraws it on
// CommandMenuRevisionSetting: its top and bottom edges in the original's screen lines (0 at the
// middle of its 240; before it opens and once it has closed, the first step's, above the screen)
// and whether the hovered command shows its second frame.
inline constexpr std::string_view CommandMenuRevisionSetting = "battle.menu.motion";
inline constexpr std::string_view CommandMenuTopSetting = "battle.menu.top";
inline constexpr std::string_view CommandMenuBottomSetting = "battle.menu.bottom";
inline constexpr std::string_view CommandMenuBlinkSetting = "battle.menu.blink";

// The motion of the player's command strip (command_menu.c: BTL_initializeCommandMenu,
// BTL_tickCommandMenu, BTL_isCommandMenuClosed). The strip drops in from above the screen: its
// bottom edge comes down in 7 steps, stretching it, then its top edge follows in 7 more, the two
// overlapping on one frame, 13 frames in all. It goes back up the same way, top edge first, when
// the battle ends. Meanwhile the hovered command's icon swaps to its second frame and back every 8
// frames, counted from the strip's opening, while the fight is on (GAME_STATE 1). Frame by frame at
// the battle's rate.
class CommandMenuMotion {
public:
  // BTL_initializeCommandMenu: the strip starts above the screen.
  void open();
  // BTL_COMMAND_MENU_ACTIVE = 0: the strip goes back up.
  void close();
  // BTL_tickCommandMenu: one battle frame; fighting is the original's GAME_STATE 1, the only one
  // that blinks.
  void step(bool fighting);
  // BTL_isCommandMenuClosed, asked once a frame before the frame runs: true once the strip is back
  // up and 3 more frames have passed, so the battle ends on the 4th ask.
  [[nodiscard]] bool closed();

  [[nodiscard]] bool shown() const { return state_ != State::Gone; }
  // BTL_COMMAND_MENU_ACTIVE == 1: the strip is down and has not been told to go back up.
  [[nodiscard]] bool active() const { return state_ == State::Open; }
  // The strip's edges in the original's screen lines, 0 at the middle (BTL_COMMAND_MENU_X is the
  // top, BTL_COMMAND_MENU_Y the bottom, as BTL_renderCommandMenu draws them).
  [[nodiscard]] std::int32_t top() const;
  [[nodiscard]] std::int32_t bottom() const;
  [[nodiscard]] bool blink() const { return blink_; }
  // BTL_COMMAND_MENU_TIMER: the frames since the strip opened, as a byte.
  [[nodiscard]] std::int32_t frames() const { return timer_; }

  [[nodiscard]] ScreenSettings settings() const;

private:
  enum class State : std::uint8_t { Gone, Open, Closing };

  State state_ = State::Gone;
  // BTL_COMMAND_MENU_Y_STEP and _X_STEP: the bottom's and the top's step, 0 to 7.
  std::int32_t bottomStep_ = 0;
  std::int32_t topStep_ = 0;
  // BTL_COMMAND_MENU_TIMER, a byte.
  std::uint8_t timer_ = 0;
  bool blink_ = false;
  // BTL_COMMAND_MENU_CLOSED_FRAMES.
  std::int32_t closedFrames_ = 0;
};

} // namespace dwe::screens
