#pragma once

#include <dwe/game/coop/protocol.hpp>
#include <dwe/net/peer_link.hpp>

#include <deque>
#include <functional>
#include <optional>
#include <string>

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
  // The game is under way and closed to new players.
  RoomClosed,
  Damaged,
  Disconnected
};

// The host's answer to a guest's hello (CoopLink::admit): the seat it gets, or why not.
struct Admission {
  RejectReason refused = RejectReason::None;
  PlayerId id = GuestPlayerId;
  // The seat's key for the welcome (Welcome::seatKey).
  std::uint64_t seatKey = 0;
};

struct LinkTimeouts {
  net::Millis answer{10000};
  net::Millis silence{8000};
  // How long a side busy with a long load (it does not poll) keeps its line alive anyway
  // (net::PeerLink::keepAliveWhileBusy), so the others wait for it instead of dropping it after
  // `silence`; past it, the game counts as hung and the silence decides.
  net::Millis busy{90000};
};

// Owns the coop handshake and moves protocol messages across the engine's reliable/unreliable
// lanes. World and save interpretation stay in the app/session layer.
class CoopLink {
public:
  // `playerName`: the host's name (its online profile), told to the guest in the welcome.
  // `guestId`: the player id the host's registry gives the guest on this link (one link per guest).
  static CoopLink host(net::PeerLink& link, Identity identity, std::string playerName = {},
                       LinkTimeouts timeouts = {}, PlayerId guestId = GuestPlayerId);
  // `rejoinKey`: the seat key of the room the guest dropped from, when it comes back (0: new).
  static CoopLink guest(net::PeerLink& link, Identity identity, std::string playerName,
                        LinkTimeouts timeouts = {}, std::uint64_t rejoinKey = 0);

  void poll(net::Millis now);
  [[nodiscard]] LinkPhase phase() const { return phase_; }
  [[nodiscard]] LinkFailure failure() const { return failure_; }
  [[nodiscard]] std::optional<PlayerId> remotePlayer() const { return remotePlayer_; }
  // This side's own player id: the host's, or the one the host's welcome gave this guest.
  [[nodiscard]] PlayerId localPlayer() const { return localPlayer_; }
  // On the host: the id this link's guest gets in the welcome. A room of up to four players keeps
  // a link waiting for its next guest and gives it the lowest free id until that guest says hello
  // (#352); once welcomed it no longer changes.
  [[nodiscard]] PlayerId guestId() const { return guestId_; }
  void assignGuest(PlayerId id);
  // On the host: who decides the seat of this link's guest once its hello says who it is (a guest
  // coming back with its seat key, a new player while the game is under way). Without it the
  // guest gets guestId() and no seat key.
  void admit(std::function<Admission(const Hello&)> decide) { admission_ = std::move(decide); }
  // The key to this link's seat: the one its welcome gave (on either side); 0 before it, or from
  // an older host.
  [[nodiscard]] std::uint64_t seatKey() const { return seatKey_; }
  // Why the other side said it left (once the phase is Left by its message); none when the link
  // dropped without a word.
  [[nodiscard]] std::optional<LeaveReason> leaveReason() const { return leaveReason_; }
  // The other player's name (its online profile): the guest's, from its hello, on the host; the
  // host's, from its welcome, on the guest. Empty until the link is ready, or from an older host.
  [[nodiscard]] const std::string& remoteName() const { return remoteName_; }
  [[nodiscard]] std::optional<Message> receive();
  core::Result<void> send(const Message& message);
  void leave(LeaveReason reason = LeaveReason::Quit);

private:
  CoopLink(net::PeerLink& link, Side side, Identity identity, LinkTimeouts timeouts);
  void handle(const Message& message);
  void fail(LinkFailure failure, bool sendLeave = false);
  // Rejected, failed or left: the first of these holds, whatever the transport does next.
  [[nodiscard]] bool ended() const {
    return phase_ == LinkPhase::Rejected || phase_ == LinkPhase::Failed ||
           phase_ == LinkPhase::Left;
  }

  net::PeerLink* link_;
  Side side_;
  Identity identity_;
  LinkTimeouts timeouts_;
  LinkPhase phase_ = LinkPhase::Connecting;
  LinkFailure failure_ = LinkFailure::None;
  std::optional<PlayerId> remotePlayer_;
  PlayerId localPlayer_ = HostPlayerId;
  PlayerId guestId_ = GuestPlayerId;
  std::optional<LeaveReason> leaveReason_;
  std::function<Admission(const Hello&)> admission_;
  std::uint64_t seatKey_ = 0;
  std::string remoteName_;
  // This side's own name, told to the guest (on the host).
  std::string playerName_;
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
