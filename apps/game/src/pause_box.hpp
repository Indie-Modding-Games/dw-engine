#pragma once

#include <string_view>

namespace dwe::app {

inline constexpr std::string_view PauseBoxDocument = "pause_box";

// What one tick of the pause did.
struct PauseStep {
  // The game waits this tick: neither the field, the battle nor the menus run.
  bool paused = false;
  // The box opens this tick (createPauseBox, which plays playSound(0, 3), the confirm sound).
  bool opened = false;
  // The box closes this tick (removePauseBox, silent).
  bool closed = false;
};

// Start's pause (utils2.c: handlePause, createPauseBox, removePauseBox): a press freezes the whole
// game, menus and battle included, under a small box saying "Pause"; the next press goes on. There
// is no pause on the title, over a movie or in a naming box, where Start has other uses. The app
// shows the box (PauseBoxDocument) and skips the game's tick while paused.
class PauseBox {
public:
  // One logic tick: `pressed` is the pause action this tick and `allowed` whether a game is being
  // played where the original pauses.
  PauseStep tick(bool pressed, bool allowed);
  [[nodiscard]] bool paused() const { return paused_; }

private:
  bool paused_ = false;
};

} // namespace dwe::app
