#include <dwe/assets/map_document.hpp>
#include <dwe/core/collision_triggers.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/partner/state.hpp>

#include <optional>

#include "field_toilet.hpp"

namespace dwe::app {

bool goToToiletOnCell(session::GameSession& session, const session::FieldItemsAdapter& fieldItems,
                      const game::field::Field& field) {
  const assets::CollisionCell cell = assets::collisionCellAt(field.player().position);
  if (field.collision().trigger(cell).value_or(0) != core::collision::ToiletTrigger ||
      !session.partner().state().conditions.has(game::partner::Condition::NeedsToilet)) {
    return false;
  }
  core::logDebug("[port:toilet] checkMapInteraction (tamer.c:689): toilet cell ({}, {}): the "
                 "partner goes (section 0x4e2 stands in for handleToilet)",
                 cell.column, cell.row);
  session.useToilet();
  const std::optional<session::SoundCue> cue = fieldItems.toiletSound();
  if (cue && session.sound() != nullptr) {
    session.sound()->playSound(cue->slot, cue->sound);
  }
  return true;
}

} // namespace dwe::app
