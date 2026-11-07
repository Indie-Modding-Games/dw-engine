#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace dwe::app {

// Batch training, an engine improvement (docs/modulos/apps/game.md, «Sesiones de gimnasio»): a
// course the story starts at a gym runs this many sessions in a row instead of one. Each session is
// the original's (its gains, the hour it costs, the tiredness and what the partner does after it),
// only without asking again; 1 is the original. A UI setting and the pause menu's command with the
// same name.
inline constexpr std::string_view GymSessionsSetting = "gym_sessions";
inline constexpr int GymSessionsMin = 1;
inline constexpr int GymSessionsMax = 5;

// What the field is doing when the next session of a batch could start.
struct GymBatchMoment {
  // Nothing holds the field: no course, no stats box, no script or text, no menu, no battle and no
  // scene of the partner's (the pile, the toilet, eating, praise, sleep, evolution, death).
  bool fieldIdle = false;
  // The partner shows a need or a state (hungry, sleepy, tired, the toilet, sulking, injured,
  // sick): the player would see to it before asking for another session, so the batch stops.
  bool partnerNeedsCare = false;
  // The map the player is on: leaving the gym's map stops the batch.
  std::string_view map;
};

// The sessions a batch has left: begun when the story starts a course, one taken each time the
// field is idle after a session, stopped by the player's cancel, a need of the partner's or a map
// change.
class GymBatch {
public:
  // The sessions a course runs in a row, from the option; clamped to GymSessionsMin..Max. It counts
  // from the next course the story starts.
  void setSessions(int sessions);
  [[nodiscard]] int sessions() const { return sessions_; }

  // The story started `course` on `map` (call_routine 7): its first session runs now.
  void begin(std::string_view course, std::string_view map);
  // The player ended a session early: the batch ends with it.
  void cancel() { left_ = 0; }
  // The course to start now, if the batch has one and the field allows it; nothing while the field
  // is busy, and nothing ever again once the batch stopped.
  [[nodiscard]] std::optional<std::string> next(const GymBatchMoment& moment);
  // The sessions still to run after the current one.
  [[nodiscard]] int left() const { return left_; }

private:
  int sessions_ = GymSessionsMin;
  int left_ = 0;
  std::string course_;
  std::string map_;
};

} // namespace dwe::app
