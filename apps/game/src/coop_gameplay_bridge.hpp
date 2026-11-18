#pragma once

#include "coop_commands.hpp"
#include "coop_snapshot_interpolation.hpp"

#include <dwe/game/coop/profile_transfer.hpp>

#include <deque>
#include <span>

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
  // Sends a reliable host-to-guest world Event (see game/coop/protocol). Only the host sends.
  [[nodiscard]] core::Result<void> sendEvent(const game::coop::Event& event);
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
  // Kept for tools/tests that sample an explicit interpolation fraction. Runtime presentation
  // uses the timestamp overload so packet cadence, not the local tick alpha, drives smoothing.
  [[nodiscard]] std::optional<PresentedCoopWorld> presentedWorld(float alpha) const;
  [[nodiscard]] std::optional<PresentedCoopWorld> presentedWorld(net::Millis now) const;
  // The raw latest authoritative snapshot the client reconciles its predicted guest against: its
  // acknowledgedCommand names the guest command the host last applied, and its entities carry that
  // command's authoritative result. Uninterpolated, so the reconcile compares like with like.
  [[nodiscard]] std::optional<game::coop::Snapshot> latestSnapshot() const { return latestSnapshot_; }
  // The sequence of the last field command this side handed to the host, so the client can key its
  // predicted guest states to the sequence the host will acknowledge.
  [[nodiscard]] std::optional<std::uint32_t> lastCommandSequence() const {
    return outgoingCommandSequence_ == 0 ? std::nullopt : std::optional{outgoingCommandSequence_};
  }
  // Forgets the raw latest snapshot: called when the guest changes map, so a reconcile never
  // compares an authoritative state from another map against the newly loaded field.
  void dropSnapshot() {
    latestSnapshot_.reset();
    snapshots_.reset();
  }

private:
  game::coop::CoopLink* link_;
  NetworkFieldController* remoteController_;
  std::deque<game::coop::Message> incoming_;
  game::coop::ProfileAssembler profileAssembler_;
  std::deque<ReceivedProfile> profiles_;
  std::optional<std::uint32_t> profileTransferId_;
  CoopSnapshotInterpolation snapshots_;
  std::optional<game::coop::Snapshot> latestSnapshot_;
  std::uint32_t outgoingCommandSequence_ = 0;
};

} // namespace dwe::app
