#pragma once

#include <dwe/net/peer_link.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

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
// (wss://host[:port][/prefix]).
std::string hostUrl(std::string_view server);
std::string joinUrl(std::string_view server, std::string_view code);

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

} // namespace dwe::net
