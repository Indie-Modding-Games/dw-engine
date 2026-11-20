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
  // The host's snapshots smoothed for a frame drawn at `now` (the poll clock).
  [[nodiscard]] std::optional<PresentedCoopWorld> presentedWorld(net::Millis now) const;
  // Presence on a guest (for what is shared per map, such as battles): the players the host's
  // newest snapshot shows on `mapId`. The host asks its CoopHostWorld's authority instead.
  [[nodiscard]] std::vector<game::coop::PlayerId> playersOn(std::string_view mapId) const {
    return snapshots_.playersOn(mapId);
  }
  // The shared world's messages received since the last call, in order: the guests' states and
  // world asks on the host; the host's corrections, rosters, world changes and syncs and ground
  // items on a guest (#353, #354, #355, #357).
  [[nodiscard]] std::vector<game::coop::Message> takeWorldMessages();
  // This side's own player state, sent unreliably (only the newest matters).
  [[nodiscard]] core::Result<void> send(const game::coop::Message& message);

private:
  game::coop::CoopLink* link_;
  NetworkFieldController* remoteController_;
  std::deque<game::coop::Message> incoming_;
  std::deque<game::coop::Message> battles_;
  game::coop::ProfileAssembler profileAssembler_;
  std::deque<ReceivedProfile> profiles_;
  std::optional<std::uint32_t> profileTransferId_;
  CoopSnapshotInterpolation snapshots_;
  std::vector<game::coop::Message> world_;
};

} // namespace dwe::app
