#pragma once

#include <dwe/net/peer_link.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <span>
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
// The most players a room seats, its host included (apps/versus-signal/src/seats.ts).
inline constexpr int MaxRoomSeats = 4;

// What a public room's host tells the directory about it (`/v1/host?public=1&...`): its nickname
// and profile icon, the compatibility key (games only list rooms of their own) and the mode. The
// server takes a nickname of directoryName()'s letters only.
struct PublicHost {
  std::string name;
  std::string icon;
  std::string compat;
  std::string mode = "campaign";
};

// A nickname as the directory takes it: letters (with the Latin accents), digits, spaces and
// . _ ' -, at most 16 letters, no spaces at the ends (apps/versus-signal/src/limits.ts). Empty when
// nothing of it is left: the room is listed without a name.
std::string directoryName(std::string_view name);

// A directory compatibility key: games list only rooms they can play. Made of the mode, the game
// version and the first 16 letters of the content's identity, with any letter the server does
// not take (A-Z a-z 0-9 . _ -) as '-', at most 64 letters ("coop-0.1.0-beta.12-0123456789abcdef").
std::string compatKey(std::string_view mode, std::string_view version, std::string_view content);

// A text for a URL's query, with every byte but the unreserved ones as %XX.
std::string percentEncoded(std::string_view text);

// Where a host makes its room and where a guest joins one, on the server `server`
// (wss://host[:port][/prefix]). A listed room goes in the server's public list
// (`/v1/host?public=1`); any other room is only reached by its code.
// `seats`: how many players the room seats, host included (2 to MaxRoomSeats); a room of more
// than two keeps taking guests through its host's next seats (seatUrl).
std::string hostUrl(std::string_view server, bool listed = false, int seats = 2);
std::string hostUrl(std::string_view server, const PublicHost& listing, int seats = 2);
// Where the host of a room of more than two seats waits for its next guest, with the room's token
// and how many guests it holds now (`taken`): the server pairs the next guest with it.
std::string seatUrl(std::string_view server, std::string_view code, std::string_view token,
                    int taken);
std::string joinUrl(std::string_view server, std::string_view code);
// Where the public list is read: the server answers a WebSocket there with one snapshot of it,
// of the rooms of one compatibility key (all without one).
std::string roomsUrl(std::string_view server, std::string_view compat = {});
// Where the server says what it can do (`/v1/info`), in one message.
std::string infoUrl(std::string_view server);
// Where a public room's host tells the directory how the room goes ("ready", "playing" or
// "closed"), with its token: the server answers {"type":"state"} or an error, and closes.
std::string stateUrl(std::string_view server, std::string_view code, std::string_view token,
                     std::string_view state);

// A relay (TURN) server the signaling server handed out with `room` or `joined` (short-lived
// credentials; none when the server has no TURN).
struct IceServer {
  std::vector<std::string> urls;
  std::string username;
  std::string credential;

  friend bool operator==(const IceServer&, const IceServer&) = default;
};

// The relay servers as libdatachannel's ICE URLs ("turn:user:pass@host:port?transport=udp", the
// credentials percent-encoded). Relays over UDP only: the library's ICE agent (libjuice) does not
// relay over TCP or TLS, so those URLs are left out.
std::vector<std::string> relayUrls(std::span<const IceServer> servers);

// A room in the server's public list (apps/versus-signal/src/room.ts, RoomListing).
struct PublicRoom {
  std::string roomId;
  std::string joinCode;
  std::string name;
  std::string message;
  int players = 0;
  int capacity = 0;
  double createdAt = 0.0;
  // The first protocol's state: "waiting" or "full".
  std::string state;
  // The room's phase: "open", "connected", "ready" or "playing" (empty from a first-protocol
  // server: then `state` says it).
  std::string phase;
  // The host's nickname and icon, the compatibility key and the mode ("" when not given).
  std::string hostName;
  std::string icon;
  std::string compat;
  std::string mode;
};

// The rooms in a snapshot of the public list ({"rooms": [...]}); nothing when it is not one. A
// room without a usable code is left out.
std::optional<std::vector<PublicRoom>> parseRoomList(std::string_view text);

// What a signaling server can do (`/v1/info`): "public-rooms", "room-state", "relay".
struct ServerInfo {
  std::string protocol;
  std::vector<std::string> features;

  [[nodiscard]] bool has(std::string_view feature) const;
};

std::optional<ServerInfo> parseServerInfo(std::string_view text);

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

// Opens a text WebSocket to a URL; the tests give one to an in-process server.
using SocketOpener = std::function<std::unique_ptr<SignalSocket>(const std::string& url)>;

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
  // The host of a room of more than two seats tells how many guests it holds now, so the server
  // offers a seat that freed again, and whether its game is under way and takes new players
  // (only while this socket is still open). A server that predates games in progress ignores the
  // last two.
  void tellSeats(const SeatsNotice& notice);

  // The next JSON object the other peer relayed, if any.
  std::optional<std::string> receive();
  // The host's room code, once the server made the room.
  [[nodiscard]] const std::string& roomCode() const { return code_; }
  // Both peers are in the room (for the host, a guest arrived; for the guest, it got in).
  [[nodiscard]] bool paired() const { return paired_; }
  // Why the room cannot go on, if it cannot.
  [[nodiscard]] const std::optional<PeerFailure>& failure() const { return failure_; }
  [[nodiscard]] bool finished() const { return finished_; }
  // A public room's token, for its host's state notices (with `room`); empty otherwise.
  [[nodiscard]] const std::string& roomToken() const { return token_; }
  // The relay servers the server handed out with `room` or `joined` (none without TURN).
  [[nodiscard]] const std::vector<IceServer>& iceServers() const { return iceServers_; }

private:
  void take(std::string_view text);
  void fail(PeerFailure failure);

  std::unique_ptr<SignalSocket> socket_;
  std::string code_;
  std::string token_;
  std::vector<IceServer> iceServers_;
  bool opened_ = false;
  bool paired_ = false;
  bool finished_ = false;
  std::optional<PeerFailure> failure_;
  std::deque<std::string> inbox_;
};

// One question to the server on a socket of its own, which the server answers with one message
// and closes: the public list, /v1/info, a host's state notice. Nothing blocks.
class SignalRequest {
public:
  explicit SignalRequest(std::unique_ptr<SignalSocket> socket);

  // Reads what the server sent.
  void poll();
  [[nodiscard]] bool done() const { return answer_.has_value() || failed_; }
  // The server never answered: out of reach, or it closed first.
  [[nodiscard]] bool failed() const { return failed_; }
  // The server's answer, once it came.
  [[nodiscard]] const std::optional<std::string>& answer() const { return answer_; }
  // When the answer was {"type":"error","reason":...}: its reason ("limit", "bad-token"...).
  [[nodiscard]] std::string errorReason() const;

private:
  std::unique_ptr<SignalSocket> socket_;
  std::optional<std::string> answer_;
  bool failed_ = false;
};

// One read of the public list: opens the socket, waits for the snapshot, closes. Nothing blocks.
class RoomListRequest {
public:
  explicit RoomListRequest(std::unique_ptr<SignalSocket> socket);

  // Reads what the server sent.
  void poll();
  [[nodiscard]] bool done() const { return request_.done(); }
  // No list came: the server is out of reach, refused it (refusal()) or sent something else.
  [[nodiscard]] bool failed() const { return done() && !rooms_.has_value(); }
  [[nodiscard]] const std::optional<std::vector<PublicRoom>>& rooms() const { return rooms_; }
  // Why the server refused the list ("limit": asked too often); empty otherwise.
  [[nodiscard]] std::string refusal() const { return request_.errorReason(); }

private:
  SignalRequest request_;
  std::optional<std::vector<PublicRoom>> rooms_;
};

// One read of /v1/info. A server that does not answer it is a first-protocol one, with no public
// rooms (or out of reach: the list says which).
class ServerInfoRequest {
public:
  explicit ServerInfoRequest(std::unique_ptr<SignalSocket> socket);

  void poll();
  [[nodiscard]] bool done() const { return request_.done(); }
  [[nodiscard]] const std::optional<ServerInfo>& info() const { return info_; }

private:
  SignalRequest request_;
  std::optional<ServerInfo> info_;
};

} // namespace dwe::net
