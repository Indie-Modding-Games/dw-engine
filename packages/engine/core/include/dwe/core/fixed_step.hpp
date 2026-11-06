#pragma once

#include <algorithm>
#include <cstdint>

namespace dwe::core {

// Splits real time into fixed logic ticks. The game logic always advances by the same step,
// whatever the frame rate, and rendering interpolates between the last two ticks with alpha().
class FixedStep {
public:
  // tickSeconds is the length of one logic tick; maxFrameSeconds caps a long frame (a breakpoint, a
  // window drag) so the logic does not try to catch up all at once.
  constexpr FixedStep(double tickSeconds, double maxFrameSeconds)
      : tickNanoseconds_{toNanoseconds(tickSeconds)},
        maxFrameNanoseconds_{toNanoseconds(maxFrameSeconds)} {}

  // Adds the real time of a frame and returns how many ticks the logic must run now.
  constexpr int advance(double frameSeconds) {
    takenNanoseconds_ =
        std::clamp(toNanoseconds(frameSeconds), std::int64_t{0}, maxFrameNanoseconds_);
    accumulated_ += takenNanoseconds_;
    int ticks = 0;
    while (accumulated_ >= tickNanoseconds_) {
      accumulated_ -= tickNanoseconds_;
      ++ticks;
    }
    return ticks;
  }

  // The time the last advance took in, after the cap: how far the logic's timeline moved, the part
  // of a tick still to come included. What is drawn between ticks (animations, fades) moves on by
  // this, not by the frame's real time, so a stall or a slow machine never takes the drawing ahead
  // of the logic, and a fast display moves both by the same.
  [[nodiscard]] constexpr double takenSeconds() const {
    return static_cast<double>(takenNanoseconds_) / NanosecondsPerSecond;
  }

  // Forgets the time not yet run, after a pause the logic must not catch up on (a game that took
  // a while to load).
  constexpr void reset() {
    accumulated_ = 0;
    takenNanoseconds_ = 0;
  }

  // How far the current moment is between the last tick and the next one, from 0 to 1.
  [[nodiscard]] constexpr double alpha() const {
    return static_cast<double>(accumulated_) / static_cast<double>(tickNanoseconds_);
  }

  [[nodiscard]] constexpr double tickSeconds() const {
    return static_cast<double>(tickNanoseconds_) / NanosecondsPerSecond;
  }

private:
  static constexpr double NanosecondsPerSecond = 1e9;

  // Time is kept in whole nanoseconds: adding and subtracting doubles drifts, and a frame of
  // exactly N ticks would sometimes run N - 1.
  static constexpr std::int64_t toNanoseconds(double seconds) {
    const double nanoseconds = seconds * NanosecondsPerSecond;
    return static_cast<std::int64_t>(nanoseconds < 0.0 ? nanoseconds - 0.5 : nanoseconds + 0.5);
  }

  std::int64_t tickNanoseconds_;
  std::int64_t maxFrameNanoseconds_;
  std::int64_t accumulated_ = 0;
  std::int64_t takenNanoseconds_ = 0;
};

} // namespace dwe::core
