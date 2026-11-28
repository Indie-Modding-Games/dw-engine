#pragma once

#include <dwe/net/peer_link.hpp>
#include <dwe/net/signal.hpp>

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::net {

// How often a public room's host tells the directory its state again while the room lasts, so the
// entry of a host that crashed or lost its line leaves the list a few minutes later
// (apps/versus-signal/src/directory.ts, ROOM_REFRESH_MS and STATE_MAX_AGE_MS).
inline constexpr Millis RoomRefreshEvery{60 * 1000};

// A room's phase (docs/plan/online-rooms.md, "Room states"), the same the signaling server's
// directory keeps (apps/versus-signal/src/room.ts, RoomPhase):
// - Connecting: the room is being made or joined (no code yet, or the guest is on its way).
// - Open: the host has its code and waits for a guest.
// - Connected: both are in the room and open their route.
// - Ready: both picked what they play.
// - Playing: the game (the series, the campaign) is under way.
// - Closed: it ended, someone left, it failed or it expired. Nothing leaves Closed.
enum class RoomPhase : std::uint8_t { Connecting, Open, Connected, Ready, Playing, Closed };

[[nodiscard]] std::string_view roomPhaseName(RoomPhase phase);

struct RoomSessionDesc {
  bool hosting = false;
  // The room is in the public list. A private room sends no notices, though a room of more than two
  // seats gets a token for its next seats.
  bool listed = false;
  // The code the guest joins with (the host gets its own from the server).
  std::string code;
  // The signaling server and how to open sockets to it, for a public room host's notices (empty:
  // none are sent).
  std::string server;
  SocketOpener open;
};

// The room a player is in, as an explicit phase instead of what a screen or a link implies. The
// flows (VersusFlow, the co-op connection) follow their link with observe() and say what they
// know only themselves (ready, playing, closed). A public room's host tells the directory each of
// those (`POST /v1/rooms/<code>/state` with the room's token, sent over a WebSocket one at a time);
// a notice that fails is logged and the room goes on: the directory's entry then expires by
// itself. Nothing blocks: poll() moves the notices on, also once the room is closed, so keep the
// session until noticesPending() is false to let «closed» reach the server.
class RoomSession {
public:
  explicit RoomSession(RoomSessionDesc desc);

  // Follows the room's link: its code (Open), the other peer (Connected), its failure (Closed).
  void observe(const PeerLink& link);
  // Both players picked what they play (Connected -> Ready), or one took it back (Ready ->
  // Connected).
  void markReady(bool ready);
  // The game started (from Connected or Ready).
  void markPlaying();
  // The room ended: left, finished or failed (`reason`, an error code, empty when it just ended).
  void close(std::string reason = {});
  // Moves the host's notices on.
  void poll();
  // poll(), and once the guests are in, tells the directory the room's state again every
  // RoomRefreshEvery.
  void poll(Millis now);

  [[nodiscard]] RoomPhase phase() const { return phase_; }
  [[nodiscard]] bool hosting() const { return desc_.hosting; }
  [[nodiscard]] const std::string& code() const { return code_; }
  // A public room: its host has a token for the directory.
  [[nodiscard]] bool listed() const { return desc_.listed && !token_.empty(); }
  // Why it closed (the link's failure, if that was it), and the reason given to close().
  [[nodiscard]] PeerFailure failure() const { return failure_; }
  [[nodiscard]] const std::string& closeReason() const { return reason_; }
  [[nodiscard]] bool noticesPending() const { return notice_ != nullptr || !notices_.empty(); }
  // The last notice the server refused or never answered, by its error ("bad-token",
  // "no-room", "unreachable"...); empty while they all went through.
  [[nodiscard]] const std::string& noticeError() const { return noticeError_; }
  // The notices the server took, in order ("connected", "ready", "playing", "closed").
  [[nodiscard]] const std::deque<std::string>& sentNotices() const { return sent_; }

private:
  // Moves to `next` if the room's phases allow it; false (and nothing changes) otherwise.
  bool enter(RoomPhase next);
  void notify(std::string state);

  RoomSessionDesc desc_;
  RoomPhase phase_ = RoomPhase::Connecting;
  std::string code_;
  std::string token_;
  PeerFailure failure_ = PeerFailure::None;
  std::string reason_;
  // Ever reached Connected: the directory's entry outlives the room then, so «closed» is told.
  bool connectedOnce_ = false;
  std::deque<std::string> notices_;
  std::unique_ptr<SignalRequest> notice_;
  std::string noticeState_;
  std::string noticeError_;
  std::deque<std::string> sent_;
  std::optional<Millis> toldAt_;
  // A server that predates the «connected» notice refused it: the lobby is not repeated to it.
  bool connectedRefused_ = false;
};

} // namespace dwe::net
