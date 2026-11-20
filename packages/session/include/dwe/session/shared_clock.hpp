#pragma once

#include <cstdint>
#include <optional>

// The world's clock in co-op (docs/architecture/coop-training.md), an engine addition: the
// original has one player and one clock.
namespace dwe::session {

// Who keeps a game's clock.
enum class ClockShare : std::uint8_t {
  // Single player: the game's own clock, as the original's.
  Own,
  // A co-op host: its clock is the world's, told to the guests. A training course neither stops nor
  // moves it.
  Host,
  // A co-op guest: its clock follows the host's.
  Guest,
};

// The host's clock as a guest last heard it.
struct WorldTime {
  std::int64_t elapsedFrames = 0;
  bool running = false;
  double speed = 1.0;

  friend bool operator==(const WorldTime&, const WorldTime&) = default;
};

// What one tick of a guest's clock does to follow the world's.
struct ClockFollow {
  // Frames to play, as playing them: the partner's care runs over them.
  std::int64_t frames = 0;
  // Whole hours the world skipped at once (the host slept): taken as a time skip, with the
  // partner's skipped-hours accounting, not played frame by frame.
  std::int32_t skippedHours = 0;
  // The guest's own clock went past the world's (its own sleep, a solo battle's time): it goes back
  // to the world's time, and what its partner already lived is not lived again.
  std::optional<std::int64_t> backTo;

  friend bool operator==(const ClockFollow&, const ClockFollow&) = default;
};

// A guest's view of the world's clock: the host's newest word and the real time since, played on
// at the host's speed while the host said it runs, at most MaxLeadSeconds past the word (a quiet
// link does not run away with it).
class WorldClockFollower {
public:
  static constexpr double MaxLeadSeconds = 2.0;
  // A guest this many game minutes or more past the world goes back to it; less waits for it.
  static constexpr std::int64_t BackAfterMinutes = 5;

  void hear(const WorldTime& time);
  // Real seconds passed on the guest.
  void pass(double realSeconds);
  [[nodiscard]] bool heard() const { return heard_.has_value(); }
  [[nodiscard]] std::optional<WorldTime> lastHeard() const { return heard_; }
  // The world's time now, in frames; none before the first word.
  [[nodiscard]] std::optional<std::int64_t> worldFrames(double framesPerSecond) const;
  // The step from the guest's clock at `elapsed` towards the world's.
  [[nodiscard]] ClockFollow follow(std::int64_t elapsed, double framesPerSecond,
                                   std::int64_t framesPerMinute) const;
  void reset();

private:
  std::optional<WorldTime> heard_;
  double since_ = 0.0;
};

// The host's side: when the world's clock is worth telling again (about once a second, and at once
// when it starts, stops, changes speed or jumps).
class WorldClockTeller {
public:
  static constexpr double EverySeconds = 1.0;

  // The world's time after a tick of `realSeconds`; true when it is to be told now.
  [[nodiscard]] bool due(const WorldTime& now, double realSeconds, double framesPerSecond);
  // A new guest hears the clock on the next tick.
  void tellAgain() { told_.reset(); }

private:
  std::optional<WorldTime> told_;
  double since_ = 0.0;
};

} // namespace dwe::session
