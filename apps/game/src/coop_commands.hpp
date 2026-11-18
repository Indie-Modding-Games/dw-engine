#pragma once

#include "player_commands.hpp"

#include <dwe/game/coop/link.hpp>
#include <dwe/game/coop/protocol.hpp>

namespace dwe::app {

// The network protocol deliberately carries only the field controls in this milestone. Story,
// fishing and UI controls remain local until their authoritative action messages are implemented.
[[nodiscard]] core::Result<game::coop::Commands> toCoopCommand(const PlayerCommands& commands,
                                                              std::uint32_t sequence = 0);
[[nodiscard]] PlayerCommands fromCoopCommand(const game::coop::Commands& commands);

// Adapts already-validated network field commands to the app's controller slot. CoopLink owns
// transport and role checks; this controller owns only freshness and the PlayerCommands mapping.
class NetworkFieldController final : public PlayerController {
public:
  [[nodiscard]] bool receive(const game::coop::Commands& command, net::Millis receivedAt) {
    return buffer_.push(command, receivedAt);
  }
  void setNow(net::Millis now) { now_ = now; }
  [[nodiscard]] std::optional<std::uint32_t> latestSequence() const {
    return buffer_.sequence();
  }
  [[nodiscard]] PlayerCommands next(std::uint64_t /*tick*/) override;

private:
  game::coop::RemoteCommandBuffer buffer_;
  net::Millis now_{0};
};

} // namespace dwe::app
