#pragma once

#include <dwe/net/datagram.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace dwe::net {

// The two ways a message can travel between the peers.
enum class Lane : std::uint8_t {
  // Each message arrives once and in the order sent (the handshake, the lineups, the checksums).
  Reliable,
  // A message may arrive late, out of order or never; the sender repeats what matters (the
  // lockstep's inputs).
  Unreliable,
};

// Where a link is on its way to the other peer.
enum class PeerState : std::uint8_t {
  // Looking for the other peer: making the room, waiting for a guest, or opening the route.
  Connecting,
  // Both lanes carry messages.
  Open,
  // It will not open or it closed: failure() says why.
  Failed,
};

enum class PeerFailure : std::uint8_t {
  None,
  // No signaling server is set up in this game.
  NoServer,
  // The signaling server could not be reached or dropped the connection.
  ServerUnreachable,
  // No room has that code.
  WrongCode,
  // The room already has its two players.
  RoomFull,
  // The room waited too long for a guest, or the two peers too long for a route.
  TimedOut,
  // The peers could not find a direct route to each other (ICE failed).
  NoRoute,
  // The connection closed after it was open.
  Closed,
};

// One end of a connection to the other peer, with a reliable ordered lane and an unreliable one.
// Nothing here blocks: poll() moves it on and the messages wait in it until received. Messages
// sent on the reliable lane before it opens wait for it; on the unreliable lane they are dropped.
class PeerLink {
public:
  PeerLink() = default;
  PeerLink(const PeerLink&) = delete;
  PeerLink& operator=(const PeerLink&) = delete;
  PeerLink(PeerLink&&) = delete;
  PeerLink& operator=(PeerLink&&) = delete;
  virtual ~PeerLink() = default;

  virtual void send(Lane lane, std::span<const std::uint8_t> message) = 0;
  // The next message that arrived on a lane, if any.
  virtual std::optional<Bytes> receive(Lane lane) = 0;
  // Reads what arrived, sends what is due and checks the timeouts.
  virtual void poll(Millis now) = 0;
  [[nodiscard]] virtual PeerState state() const = 0;
  [[nodiscard]] virtual PeerFailure failure() const = 0;
  // When anything last came from the other peer, once it has.
  [[nodiscard]] virtual std::optional<Millis> lastHeard() const = 0;
  // The room's code for the guest to type, once the host has one; empty otherwise.
  [[nodiscard]] virtual std::string roomCode() const { return {}; }
  // Whether the other peer reached the room (for the host: a guest typed its code).
  [[nodiscard]] virtual bool peerFound() const { return state() == PeerState::Open; }
};

} // namespace dwe::net
