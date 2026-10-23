#pragma once

#include <dwe/game/field/field.hpp>
#include <dwe/session/field_items_adapter.hpp>
#include <dwe/session/game_session.hpp>

namespace dwe::app {

// The toilet cell (tamer.c: checkMapInteraction): while the player stands on a cell of value 120
// and the partner needs the toilet, the partner goes (map section 0x4e2: call_partner_routine(0),
// partner state 6, partner_impl.c: handleToilet; then play_sound(0, 10)). Until the story's
// runner runs that section, this goes straight to handleToilet's effect: the partner does not walk
// to the toilet first. Returns whether the partner went.
bool goToToiletOnCell(session::GameSession& session, const session::FieldItemsAdapter& fieldItems,
                      const game::field::Field& field);

} // namespace dwe::app
