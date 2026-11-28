#pragma once

#include <dwe/game/coop/protocol.hpp>
#include <dwe/game/field/field.hpp>

#include <span>

namespace dwe::app {

// Clips past 16 bits are not sent.
[[nodiscard]] std::optional<game::coop::ActionClip>
coopActionOf(std::optional<game::field::PlayedClip> clip);
[[nodiscard]] std::optional<game::field::PlayedClip>
playedClipOf(std::optional<game::coop::ActionClip> action);

[[nodiscard]] core::Result<game::coop::EntityState>
toCoopEntityState(std::uint16_t networkId, game::coop::PlayerId owner, game::coop::EntityRole role,
                  const game::field::CharacterState& state);

// Captures every player and partner of one authoritative field tick (network ids 2n+1 for player
// n and 2n+2 for its partner: 1 and 2 the host's, 3 and 4 the first guest's). NPCs are not
// replicated yet.
// `away` (CoopHostWorld::away) are left out, so the guests do not draw them.
[[nodiscard]] core::Result<game::coop::Snapshot>
makeCoopSnapshot(const game::field::Field& field, std::string mapId, std::uint32_t worldTick,
                 std::uint32_t acknowledgedCommand,
                 std::span<const game::coop::PlayerId> away = {});

} // namespace dwe::app
