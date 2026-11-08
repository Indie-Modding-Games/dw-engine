#pragma once

#include <glm/vec2.hpp>

#include <cstdint>
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
// How far the player may walk from where a session left it before the batch stops (world units,
// five collision cells): room to feed the partner or clean a pile beside the machine, not to go
// elsewhere on the map.
inline constexpr float GymBatchLeash = 500.0F;

// What the field is doing when the next session of a batch could start.
struct GymBatchMoment {
  // Nothing holds the field: no course, no stats box, no script or text, no menu, no battle and no
  // scene of the partner's (the pile, the toilet, eating, praise, sleep, evolution, death).
  bool fieldIdle = false;
  // The partner shows a need the player can see to (hungry, the toilet, sleepy, tired): the batch
  // waits until it is gone.
  bool partnerNeedsCare = false;
  // The partner shows a state a session should not run with (sulking, injured, sick): the batch
  // stops.
  bool partnerUnfit = false;
  // The map the player is on: leaving the gym's map stops the batch.
  std::string_view map;
  // Where the player stands on the ground (x, z).
  glm::vec2 player{};
};

// Why a batch stopped before its last session.
enum class GymBatchStop : std::uint8_t { None, Cancelled, MapChanged, PartnerUnfit, WalkedAway };

class GymBatch {
public:
  // The sessions a course runs in a row, from the option; clamped to GymSessionsMin..Max. It counts
  // from the next course the story starts.
  void setSessions(int sessions);
  [[nodiscard]] int sessions() const { return sessions_; }

  // The story started `course` on `map` (call_routine 7): its first session runs now.
  void begin(std::string_view course, std::string_view map);
  // The player ended a session early: the batch ends with it.
  void cancel() { stop(GymBatchStop::Cancelled); }
  // The course to start now, if the batch has one and the field allows it; nothing while the field
  // is busy or the partner needs care, and nothing ever again once the batch stopped. The first
  // moment after a session marks where the player stands; walking farther than GymBatchLeash from
  // there stops the batch.
  [[nodiscard]] std::optional<std::string> next(const GymBatchMoment& moment);
  // The sessions still to run after the current one.
  [[nodiscard]] int left() const { return left_; }
  // Whether the last moment kept the batch for the partner's needs.
  [[nodiscard]] bool waitingForCare() const { return waitingForCare_; }
  // Why the batch stopped early, or None.
  [[nodiscard]] GymBatchStop stopped() const { return stopped_; }

private:
  void stop(GymBatchStop why);

  int sessions_ = GymSessionsMin;
  int left_ = 0;
  std::string course_;
  std::string map_;
  std::optional<glm::vec2> anchor_;
  bool waitingForCare_ = false;
  GymBatchStop stopped_ = GymBatchStop::None;
};

} // namespace dwe::app
