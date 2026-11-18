#pragma once

#include <dwe/game/coop/protocol.hpp>
#include <dwe/game/field/field.hpp>

namespace dwe::app {

[[nodiscard]] core::Result<game::coop::EntityState>
toCoopEntityState(std::uint16_t networkId, game::coop::PlayerId owner,
                  game::coop::EntityRole role, const game::field::CharacterState& state);

// Captures the player bodies and the host-owned partner for one authoritative field tick. NPCs and
// guest-partner replication are added when their ownership/profile lifecycle is integrated.
[[nodiscard]] core::Result<game::coop::Snapshot>
makeCoopSnapshot(const game::field::Field& field, std::string mapId, std::uint32_t worldTick,
                 std::uint32_t acknowledgedCommand);

} // namespace dwe::app
