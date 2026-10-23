#pragma once

#include <dwe/session/game_session.hpp>

#include "field_scene.hpp"

namespace dwe::app {

// The partner comes to the player before it sleeps or is praised or scolded (partner.c:
// partnerTickSleep and partnerTickPraiseScold, sub states 0 and 1): the player stands still, the
// partner walks up until it is beside the player, and the two turn to each other.
class PartnerWalkUp {
public:
  void start() { walking_ = true; }
  [[nodiscard]] bool walking() const { return walking_; }

  // One field tick with no input while the partner walks up; true on the tick it arrives, when the
  // player has turned to it. The game time runs with `clock`, and stands still without one (a
  // sleep: partner.c: partnerTickSleep stops it).
  bool tick(FieldScene& scene, float seconds, session::GameSession* clock);

private:
  bool walking_ = false;
};

} // namespace dwe::app
