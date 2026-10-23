#include "partner_walk_up.hpp"

#include <dwe/core/log.hpp>

namespace dwe::app {

bool PartnerWalkUp::tick(FieldScene& scene, float seconds, session::GameSession* clock) {
  if (!walking_) {
    return false;
  }
  if (!scene.field().partnerBeside()) {
    scene.tick({}, seconds);
    if (clock != nullptr) {
      clock->tick(seconds);
    }
    scene.finishTick();
    if (!scene.field().partnerBeside()) {
      return false;
    }
  }
  walking_ = false;
  scene.playerFacePartner();
  core::logDebug("[port:partner_call] partnerTickSleep / partnerTickPraiseScold (partner.c:291, "
                 "partner.c:383): the partner is beside the player");
  return true;
}

} // namespace dwe::app
