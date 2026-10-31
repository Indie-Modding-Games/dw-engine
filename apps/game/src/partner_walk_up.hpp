#pragma once

#include <dwe/session/game_session.hpp>

#include "field_scene.hpp"

namespace dwe::app {

// The partner comes to the player before it is praised or scolded (partner.c:
// partnerTickPraiseScold, sub states 0 and 1): the field holds the player, and the partner, unless
// it is beside the player already, walks straight up to it until it is beside
// (getPartnerTamerCloseness gives 2), the two looking at each other.
class PartnerWalkUp {
public:
  void start() {
    walking_ = true;
    holdPending_ = true;
  }
  [[nodiscard]] bool walking() const { return walking_; }

  // One field tick with no input while the partner walks up; true on the tick it arrives, when the
  // two have turned to each other. The game time runs with `clock`, and stands still without one.
  bool tick(FieldScene& scene, float seconds, session::GameSession* clock);

private:
  bool walking_ = false;
  // Whether the next tick holds the field and sends the partner to the player.
  bool holdPending_ = false;
};

} // namespace dwe::app
