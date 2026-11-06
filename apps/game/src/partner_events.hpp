#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/game/partner/events.hpp>
#include <dwe/session/game_session.hpp>

#include <span>
#include <string>
#include <string_view>

#include "field_scene.hpp"

namespace dwe::app {

// The console's and the scripts' evolve("<species>"): the partner becomes that species as a map
// script makes it (EvolutionAdapter::evolveByScript). A debug aid for the console and Lua: the
// map scripts evolve the partner through the story (session's story scenes). Applied between
// frames.
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

// Whether the partner fell sick among these events (its notice: AwardScreen::noteSickness).
[[nodiscard]] bool fellSick(std::span<const game::partner::PartnerEvent> events);

// The partner's name as the original's PARTNER_ENTITY.name holds it: the name the player gave it,
// else its species' name (`species` may be null: the species' id then).
[[nodiscard]] std::string partnerNameOf(const session::GameSession& session,
                                        const assets::DigimonTable* species);

} // namespace dwe::app
