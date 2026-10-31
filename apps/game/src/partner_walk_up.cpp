#include "partner_walk_up.hpp"

#include <dwe/core/log.hpp>

#include <utility>

namespace dwe::app {

bool PartnerWalkUp::tick(FieldScene& scene, float seconds, session::GameSession* clock) {
  if (!walking_) {
    return false;
  }
  game::field::Field& field = scene.field();
  if (std::exchange(holdPending_, false)) {
    // partner.c:371-379: sub state 0 starts the walk clip unless the partner is beside the player
    // already; the player stands since the menu opened (tamer.c: tamerSetState(1), then 6). The
    // field's follow stops 2.5 to 3.5 radii away, so the partner walks on its own up to the
    // player.
    field.holdForCare(true);
    if (!field.partnerBeside()) {
      const game::field::PartnerRules& rules = field.rules().partner;
      field.walkPartnerToRange(rules.walkDistance * rules.mover.radius);
    }
  }
  scene.tick({}, seconds);
  if (clock != nullptr) {
    clock->tick(seconds);
  }
  scene.finishTick();
  if (field.partnerWalking()) {
    return false;
  }
  // partner.c:380-396: sub state 1, the two look at each other until getPartnerTamerCloseness
  // gives 2.
  walking_ = false;
  field.faceEachOther();
  core::logDebug("[port:partner_call] partnerTickPraiseScold (partner.c:386): the partner is "
                 "beside the player");
  return true;
}

} // namespace dwe::app
