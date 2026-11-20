#include <dwe/session/shared_clock.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace dwe::session {

namespace {

constexpr std::int64_t MinutesPerHour = 60;

// The frames the world's clock runs in `seconds`, as the host said it runs.
double framesIn(const WorldTime& time, double seconds, double framesPerSecond) {
  return time.running ? seconds * framesPerSecond * std::max(time.speed, 0.0) : 0.0;
}

} // namespace

void WorldClockFollower::hear(const WorldTime& time) {
  heard_ = time;
  since_ = 0.0;
}

void WorldClockFollower::pass(double realSeconds) {
  if (heard_) {
    since_ += std::max(realSeconds, 0.0);
  }
}

std::optional<std::int64_t> WorldClockFollower::worldFrames(double framesPerSecond) const {
  if (!heard_) {
    return std::nullopt;
  }
  const double lead = framesIn(*heard_, std::min(since_, MaxLeadSeconds), framesPerSecond);
  return heard_->elapsedFrames + static_cast<std::int64_t>(std::floor(lead));
}

ClockFollow WorldClockFollower::follow(std::int64_t elapsed, double framesPerSecond,
                                       std::int64_t framesPerMinute) const {
  const auto world = worldFrames(framesPerSecond);
  if (!world) {
    return {};
  }
  const std::int64_t gap = *world - elapsed;
  if (gap > 0) {
    const std::int64_t framesPerHour = framesPerMinute * MinutesPerHour;
    const std::int64_t hours = framesPerHour > 0 ? gap / framesPerHour : 0;
    return {.frames = gap - (hours * framesPerHour),
            .skippedHours = static_cast<std::int32_t>(hours),
            .backTo = std::nullopt};
  }
  // A little ahead is the guest's own play on past the host's newest word: it waits. Its own time
  // skip puts it further ahead, and it goes back.
  const std::int64_t waits =
      std::max(BackAfterMinutes * framesPerMinute,
               static_cast<std::int64_t>(
                   std::ceil(framesIn(heard_.value(), MaxLeadSeconds, framesPerSecond))));
  if (-gap > waits) {
    return {.frames = 0, .skippedHours = 0, .backTo = *world};
  }
  return {};
}

void WorldClockFollower::reset() {
  heard_.reset();
  since_ = 0.0;
}

bool WorldClockTeller::due(const WorldTime& now, double realSeconds, double framesPerSecond) {
  since_ += std::max(realSeconds, 0.0);
  bool tell = !told_ || since_ >= EverySeconds || now.running != told_->running ||
              now.speed != told_->speed;
  if (!tell) {
    // A jump (a sleep, a battle's time, a story's time skip) is told at once.
    const double expected =
        static_cast<double>(told_->elapsedFrames) + framesIn(*told_, since_, framesPerSecond);
    tell = std::abs(static_cast<double>(now.elapsedFrames) - expected) > framesPerSecond;
  }
  if (tell) {
    told_ = now;
    since_ = 0.0;
  }
  return tell;
}

} // namespace dwe::session
