#pragma once

#include <dwe/game/coop/profile_transfer.hpp>
#include <dwe/game/coop/sync_watch.hpp>

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
  // The item trades' messages (coop_trades.hpp), in their own queue; they are sent with send.
  [[nodiscard]] std::optional<game::coop::Message> receiveTrade();
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
  // The sync diagnostics' messages (#359): the host's SyncWatch on a guest, a guest's SyncReport
  // on the host, in order.
  [[nodiscard]] std::vector<game::coop::Message> takeSyncMessages();
  // The world's clock and who trains (#365, coop_training.hpp): the host's clock and everyone's
  // training on a guest, a guest's own training on the host, in order.
  [[nodiscard]] std::vector<game::coop::Message> takeTrainingMessages();
  // What reached this side of the other's numbered streams (#359): the host's snapshots on a
  // guest (by world tick), a guest's commands on the host (by sequence).
  [[nodiscard]] const game::coop::SequenceWatch& snapshotWatch() const { return snapshotWatch_; }
  [[nodiscard]] const game::coop::SequenceWatch& commandWatch() const { return commandWatch_; }
  // How long, at `now` (the poll clock), no snapshot of the host's world came: since the last one,
  // or since this bridge first polled before any. A long quiet on a guest means the host is busy
  // (a long load), and the guest says it waits (CoopHostWait).
  [[nodiscard]] net::Millis worldQuietFor(net::Millis now) const;

private:
  game::coop::CoopLink* link_;
  NetworkFieldController* remoteController_;
  std::deque<game::coop::Message> incoming_;
  std::deque<game::coop::Message> battles_;
  std::deque<game::coop::Message> trades_;
  game::coop::ProfileAssembler profileAssembler_;
  std::deque<ReceivedProfile> profiles_;
  std::optional<std::uint32_t> profileTransferId_;
  CoopSnapshotInterpolation snapshots_;
  std::vector<game::coop::Message> world_;
  std::vector<game::coop::Message> sync_;
  std::vector<game::coop::Message> training_;
  game::coop::SequenceWatch snapshotWatch_;
  game::coop::SequenceWatch commandWatch_;
  std::optional<net::Millis> firstPolled_;
  std::optional<net::Millis> lastSnapshot_;
};

} // namespace dwe::app
