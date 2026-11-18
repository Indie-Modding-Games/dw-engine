#pragma once

#include <dwe/game/coop/protocol.hpp>
#include <dwe/net/peer_link.hpp>

#include <deque>
#include <optional>

namespace dwe::game::coop {

enum class Side : std::uint8_t { Host, Guest };
enum class LinkPhase : std::uint8_t { Connecting, Ready, Rejected, Failed, Left };
enum class LinkFailure : std::uint8_t {
  None,
  NoAnswer,
  Silence,
  ProtocolMismatch,
  VersionMismatch,
  ContentMismatch,
  RoomFull,
  Busy,
  Damaged,
  Disconnected
};

struct LinkTimeouts {
  net::Millis answer{10000};
  net::Millis silence{8000};
};

// Owns the coop handshake and moves protocol messages across the engine's reliable/unreliable
// lanes. World and save interpretation stay in the app/session layer.
class CoopLink {
public:
  static CoopLink host(net::PeerLink& link, Identity identity, LinkTimeouts timeouts = {});
  static CoopLink guest(net::PeerLink& link, Identity identity, std::string playerName,
                        LinkTimeouts timeouts = {});

  void poll(net::Millis now);
  [[nodiscard]] LinkPhase phase() const { return phase_; }
  [[nodiscard]] LinkFailure failure() const { return failure_; }
  [[nodiscard]] std::optional<PlayerId> remotePlayer() const { return remotePlayer_; }
  [[nodiscard]] std::optional<Message> receive();
  core::Result<void> send(const Message& message);
  void leave(LeaveReason reason = LeaveReason::Quit);

private:
  CoopLink(net::PeerLink& link, Side side, Identity identity, LinkTimeouts timeouts);
  void handle(const Message& message);
  void fail(LinkFailure failure, bool sendLeave = false);

  net::PeerLink* link_;
  Side side_;
  Identity identity_;
  LinkTimeouts timeouts_;
  LinkPhase phase_ = LinkPhase::Connecting;
  LinkFailure failure_ = LinkFailure::None;
  std::optional<PlayerId> remotePlayer_;
  std::deque<Message> incoming_;
  net::Millis now_{0};
  std::optional<net::Millis> opened_;
};

// Keeps the newest command the host has accepted. Sequence ordering uses the standard half-range
// comparison so uint32 sequence rollover is well-defined. Stale commands become neutral.
class RemoteCommandBuffer {
public:
  [[nodiscard]] bool push(const Commands& command, net::Millis receivedAt);
  [[nodiscard]] FieldCommand current(net::Millis now) const;
  [[nodiscard]] std::optional<std::uint32_t> sequence() const { return sequence_; }
  void reset();

private:
  std::optional<std::uint32_t> sequence_;
  FieldCommand command_;
  net::Millis receivedAt_{0};
};

} // namespace dwe::game::coop
