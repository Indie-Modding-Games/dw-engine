#pragma once

#include <dwe/game/partner/events.hpp>
#include <dwe/session/game_session.hpp>

#include <span>
#include <string_view>

#include "field_scene.hpp"

namespace dwe::app {

// The console's and the scripts' evolve("<species>"): the partner becomes that species as a map
// script makes it (EvolutionAdapter::evolveByScript). A debug aid until the map scripts run,
// applied between frames.
inline constexpr std::string_view EvolveCommand = "evolve";
// And learn_move("<move> [place]"): the partner learns that move (and those learned with it) as the
// gym's brains training teaches it and, with a place (0 to 3), equips it there, so a route can show
// a technique the partner has not learned yet.
inline constexpr std::string_view LearnMoveCommand = "learn_move";

// Shows what the partner's events change on screen: an evolution or a rebirth draws the partner's
// new species in the field. Nothing shows the others yet (the HUD's reactions come with the map
// scripts), so they are only logged.
void showPartnerEvents(std::span<const game::partner::PartnerEvent> events,
                       const session::GameSession& session, FieldScene& scene);

} // namespace dwe::app
