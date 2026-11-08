#include <doctest/doctest.h>

#include <optional>
#include <string>

#include "slow_frame_log.hpp"

using dwe::app::SlowFrameLog;

TEST_CASE("slow frames log the first of a run, then one line a second with what was left out") {
  SlowFrameLog log;
  CHECK(log.slow(30.0, 100) == std::optional<std::string>{""});
  // Every frame of the next second is slow: none gets a line of its own.
  for (std::uint64_t tick = 101; tick < 160; ++tick) {
    CHECK_FALSE(log.slow(tick == 130 ? 80.0 : 40.0, tick).has_value());
  }
  CHECK(log.slow(35.0, 160) ==
        std::optional<std::string>{" (59 more since the last line, worst 80.0 ms)"});
  // A lone hitch later on logs as it is.
  CHECK(log.slow(50.0, 1000) == std::optional<std::string>{""});
}
