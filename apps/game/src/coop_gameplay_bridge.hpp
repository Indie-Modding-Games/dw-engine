#pragma once

#include <dwe/game/coop/profile_transfer.hpp>

#include <deque>
#include <span>

#include "coop_commands.hpp"
#include "coop_snapshot_interpolation.hpp"

namespace dwe::app {

// App-layer seam between the network protocol and player controllers. It translates only field
// movement in this milestone and leaves join/snapshot/profile messages queued for session setup.
class CoopGameplayBridge {
public:
  CoopGameplayBridge(game::coop::CoopLink& link, NetworkFieldController& remoteController)
      : link_{&link}, remoteController_{&remoteController} {}

  void poll(net::Millis now);
  [[nodiscard]] core::Result<void> sendLocalField(const PlayerCommands& commands);
  [[nodiscard]] core::Result<void> sendHostSnapshot(const game::coop::Snapshot& snapshot);
  // Sends a bounded profile as reliable chunks. Retries may resend the same transfer id.
  [[nodiscard]] core::Result<void> sendProfile(game::coop::ProfilePayloadKind kind,
                                               std::uint32_t transferId,
                                               std::span<const std::uint8_t> bytes);
  [[nodiscard]] std::optional<game::coop::Message> receive();
  struct ReceivedProfile {
    game::coop::ProfilePayloadKind kind;
    std::uint32_t transferId;
    net::Bytes bytes;
  };
  [[nodiscard]] std::optional<ReceivedProfile> receiveProfile();
  // The co-op battles' messages (coop_battles.hpp), in their own queue, and sending one.
  [[nodiscard]] std::optional<game::coop::Message> receiveBattle();
  [[nodiscard]] core::Result<void> sendBattle(game::coop::Message message);
  [[nodiscard]] std::optional<PresentedCoopWorld> presentedWorld(float alpha) const;

private:
  game::coop::CoopLink* link_;
  NetworkFieldController* remoteController_;
  std::deque<game::coop::Message> incoming_;
  std::deque<game::coop::Message> battles_;
  game::coop::ProfileAssembler profileAssembler_;
  std::deque<ReceivedProfile> profiles_;
  std::optional<std::uint32_t> profileTransferId_;
  CoopSnapshotInterpolation snapshots_;
};

} // namespace dwe::app
