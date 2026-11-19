#pragma once

#include <dwe/game/coop/protocol.hpp>
#include <dwe/game/field/field.hpp>

namespace dwe::app {

[[nodiscard]] core::Result<game::coop::EntityState>
toCoopEntityState(std::uint16_t networkId, game::coop::PlayerId owner, game::coop::EntityRole role,
                  const game::field::CharacterState& state);

// Captures both players and their partners for one authoritative field tick (network ids 1 host
// player, 2 host partner, 3 guest player, 4 guest partner). NPCs are not replicated yet.
[[nodiscard]] core::Result<game::coop::Snapshot>
makeCoopSnapshot(const game::field::Field& field, std::string mapId, std::uint32_t worldTick,
                 std::uint32_t acknowledgedCommand);

} // namespace dwe::app
