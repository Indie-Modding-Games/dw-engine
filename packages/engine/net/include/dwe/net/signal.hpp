#pragma once

#include <dwe/net/peer_link.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::net {

// The room codes the signaling server hands out: four characters from an alphabet without the
// ones that look alike (0/O, 1/I).
inline constexpr std::string_view RoomAlphabet = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
inline constexpr std::size_t RoomCodeLength = 4;

// A typed room code in its canonical form (upper case, without spaces or dashes), or nothing when
// it cannot be one.
std::optional<std::string> normalizedRoomCode(std::string_view typed);

// The signaling protocol's version, in the server's paths.
inline constexpr std::string_view SignalProtocol = "v1";

// Where a host makes its room and where a guest joins one, on the server `server`
// (wss://host[:port][/prefix]). A listed room goes in the server's public list
// (`/v1/host?public=1`); any other room is only reached by its code.
std::string hostUrl(std::string_view server, bool listed = false);
std::string joinUrl(std::string_view server, std::string_view code);
// Where the public list is read: the server answers a WebSocket there with one snapshot of it.
std::string roomsUrl(std::string_view server);

// A room in the server's public list (apps/versus-signal/src/room.ts, RoomListing).
struct PublicRoom {
  std::string roomId;
  std::string joinCode;
  std::string name;
  std::string message;
  int players = 0;
  int capacity = 0;
  double createdAt = 0.0;
  // "waiting", "full" or "starting".
  std::string state;
};

// The rooms in a snapshot of the public list ({"rooms": [...]}); nothing when it is not one. A
// room without a usable code is left out.
std::optional<std::vector<PublicRoom>> parseRoomList(std::string_view text);

enum class SocketState : std::uint8_t { Connecting, Open, Closed, Failed };

// A text WebSocket to the signaling server. Nothing here blocks.
class SignalSocket {
public:
  SignalSocket() = default;
  SignalSocket(const SignalSocket&) = delete;
  SignalSocket& operator=(const SignalSocket&) = delete;
  SignalSocket(SignalSocket&&) = delete;
  SignalSocket& operator=(SignalSocket&&) = delete;
  virtual ~SignalSocket() = default;

  virtual void send(std::string text) = 0;
  virtual std::optional<std::string> receive() = 0;
  [[nodiscard]] virtual SocketState state() const = 0;
  virtual void close() = 0;
};

// The client side of the signaling protocol (apps/versus-signal/README.md): the host gets a
// room code, the guest joins it, and until both peers are connected the server relays what each
// sends (the WebRTC offer, answer and candidates, JSON objects here kept as text) to the other.
class SignalClient {
public:
  explicit SignalClient(std::unique_ptr<SignalSocket> socket);

  // Reads what the server sent.
  void poll();
  // A JSON object for the other peer.
  void relay(std::string_view data);
  // The peers are connected: the server may close the room.
  void finish();

  // The next JSON object the other peer relayed, if any.
  std::optional<std::string> receive();
  // The host's room code, once the server made the room.
  [[nodiscard]] const std::string& roomCode() const { return code_; }
  // Both peers are in the room (for the host, a guest arrived; for the guest, it got in).
  [[nodiscard]] bool paired() const { return paired_; }
  // Why the room cannot go on, if it cannot.
  [[nodiscard]] const std::optional<PeerFailure>& failure() const { return failure_; }
  [[nodiscard]] bool finished() const { return finished_; }

private:
  void take(std::string_view text);
  void fail(PeerFailure failure);

  std::unique_ptr<SignalSocket> socket_;
  std::string code_;
  bool opened_ = false;
  bool paired_ = false;
  bool finished_ = false;
  std::optional<PeerFailure> failure_;
  std::deque<std::string> inbox_;
};

// One read of the public list: opens the socket, waits for the snapshot, closes. Nothing blocks.
class RoomListRequest {
public:
  explicit RoomListRequest(std::unique_ptr<SignalSocket> socket);

  // Reads what the server sent.
  void poll();
  [[nodiscard]] bool done() const { return rooms_.has_value() || failed_; }
  [[nodiscard]] bool failed() const { return failed_; }
  [[nodiscard]] const std::optional<std::vector<PublicRoom>>& rooms() const { return rooms_; }

private:
  std::unique_ptr<SignalSocket> socket_;
  std::optional<std::vector<PublicRoom>> rooms_;
  bool failed_ = false;
};

} // namespace dwe::net
