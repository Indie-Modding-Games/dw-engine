#pragma once

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace dwe::app {

// Which slow frames get a log line: the first of a run of them and then at most one a second, the
// line saying how many it left out and the worst of those. A machine that cannot keep the pace
// (a debug build, a software renderer) would otherwise log every frame and push everything else
// out of a bug report's log tail.
class SlowFrameLog {
public:
  // At 60 ticks a second: one line a second at most.
  static constexpr std::uint64_t GapTicks = 60;

  // A slow frame of `milliseconds` at `tick`: the line's tail to log, or nothing when it is left
  // out. The tail is empty, or " (N more since the last line, worst W ms)".
  std::optional<std::string> slow(double milliseconds, std::uint64_t tick) {
    if (logged_ && tick < lastTick_ + GapTicks) {
      ++skipped_;
      worstSkipped_ = std::max(worstSkipped_, milliseconds);
      return std::nullopt;
    }
    std::string tail;
    if (skipped_ > 0) {
      tail =
          std::format(" ({} more since the last line, worst {:.1f} ms)", skipped_, worstSkipped_);
    }
    logged_ = true;
    lastTick_ = tick;
    skipped_ = 0;
    worstSkipped_ = 0.0;
    return tail;
  }

private:
  bool logged_ = false;
  std::uint64_t lastTick_ = 0;
  std::uint64_t skipped_ = 0;
  double worstSkipped_ = 0.0;
};

} // namespace dwe::app
