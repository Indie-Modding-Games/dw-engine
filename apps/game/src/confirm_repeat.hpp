#pragma once

#include <algorithm>

namespace dwe::app {

// The passing of items while a button is held down. The press moves one at once; holding keeps
// moving them, slow at first and quicker the longer the button stays down. A tap moves the one and
// stops. The item keeper drives its confirm with this (keeperMoveOne repeated): every tick it hands
// over whether confirm is down, whether it went down this tick and how long the tick was, and gets
// back how many items to move.
class ConfirmRepeat {
public:
  // `seconds` is the tick's length; `pressed` is true only on the tick the button went down. A
  // button held from before the windows opened (a confirm that just came to them) does not move:
  // the hold starts on a fresh press. Returns the items to move on this tick.
  int tick(bool down, bool pressed, double seconds) {
    if (pressed) {
      held_ = true;
      interval_ = StartSeconds;
      untilNext_ = FirstDelaySeconds;
      return 1;
    }
    if (!down) {
      stop();
      return 0;
    }
    if (!held_) {
      return 0;
    }
    int moves = 0;
    untilNext_ -= seconds;
    while (untilNext_ <= 0.0) {
      ++moves;
      interval_ = std::max(MinSeconds, interval_ * Decay);
      untilNext_ += interval_;
    }
    return moves;
  }

  // Drops the hold, so the next press starts slow again (a list that emptied mid-hold).
  void stop() {
    held_ = false;
    untilNext_ = 0.0;
  }

private:
  // The wait after the press before the first held move, and the gap of that first move; the gap
  // shrinks by `Decay` every move until it reaches its floor.
  static inline constexpr double FirstDelaySeconds = 0.45;
  static inline constexpr double StartSeconds = 0.28;
  static inline constexpr double MinSeconds = 0.05;
  static inline constexpr double Decay = 0.86;

  bool held_ = false;
  double untilNext_ = 0.0;
  double interval_ = 0.0;
};

} // namespace dwe::app
